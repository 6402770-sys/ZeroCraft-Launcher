// SPDX-License-Identifier: GPL-3.0-only
#include "ModDependencyPanel.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/ContentDraw.h"

namespace Zero {
namespace ModsUi {

using namespace Mods;

namespace {

constexpr int kRowGap = Tokens::spaceSm;
constexpr int kPillMaxText = 132;  ///< px of text in a state pill; longer versions are elided
constexpr int kSmallButtonHeight = 24;
constexpr int kSkeletonLines = 4;

/// A label that elides to its width and keeps a fixed tooltip.
class EllipsisLabel : public QLabel {
   public:
    explicit EllipsisLabel(QWidget* parent) : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }
    void setFullText(const QString& text, const QString& tip = QString())
    {
        m_full = text;
        m_tip = tip;
        refresh();
    }
    QSize minimumSizeHint() const override { return { 0, QLabel::minimumSizeHint().height() }; }

   protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        refresh();
    }
    void changeEvent(QEvent* event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
            refresh();
        }
    }

   private:
    void refresh()
    {
        const QString elided = fontMetrics().elidedText(m_full, Qt::ElideRight, qMax(0, width()));
        if (elided != text()) {
            setText(elided);
        }
        setToolTip(!m_tip.isEmpty() ? m_tip : (elided == m_full ? QString() : m_full));
    }
    QString m_full;
    QString m_tip;
};

/// The 40 px logo box: `elevated` rounded square with the mod's logo, or the generic glyph.
class IconBox : public QWidget {
   public:
    explicit IconBox(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(ModDependencyPanel::kIcon, ModDependencyPanel::kIcon);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }
    void setPixmap(const QPixmap& pixmap)
    {
        m_pixmap = pixmap;
        update();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QPainterPath shape = Paint::panelPath(QRectF(rect()), t.radiusMd);
        p.setPen(Qt::NoPen);
        p.setBrush(t.elevated);
        p.drawPath(shape);
        if (m_pixmap.isNull()) {
            p.setPen(QPen(t.border, 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
            Icons::icon(QStringLiteral("blocks"), t.textTertiary).paint(&p, QRect(rect().center().x() - 10, rect().center().y() - 10, 20, 20));
        } else {
            p.setClipPath(shape);
            Content::Draw::drawCover(&p, rect(), m_pixmap);
        }
    }

   private:
    QPixmap m_pixmap;
};

QString shortened(const QString& text, int px, const QFont& font)
{
    return QFontMetrics(font).elidedText(text, Qt::ElideRight, px);
}

QLabel* makeLabel(const QString& text, Role role, QWidget* parent, const char* tone = nullptr)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    if (tone) {
        Style::setProperty(label, "zeroTone", tone);
    }
    return label;
}

QPushButton* smallButton(const QString& text, const QString& objectName, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    Style::setProperty(button, "zeroVariant", "secondary");
    Style::setProperty(button, "zeroSize", "sm");
    button->setFixedHeight(kSmallButtonHeight);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

/// "any version", the rule, or the note for a rule ZERO could not read.
QString ruleText(const QString& predicate)
{
    if (predicate.isEmpty()) {
        return QObject::tr("any version");
    }
    if (predicate == QLatin1String("?")) {
        return QObject::tr("version rule unknown");
    }
    return predicate;
}

}  // namespace

ModDependencyPanel::ModDependencyPanel(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("zeroModDependencyPanel"));
    setFixedWidth(kWidth);
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName(tr("Mod details"));
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        m_close->setIcon(Icons::icon(QStringLiteral("x"), Theme::current().text));
        update();
    });

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(kPad, kPad, kPad, kPad);
    root->setSpacing(Tokens::spaceMd);

    auto* header = new QWidget(this);
    auto* head = new QHBoxLayout(header);
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(Tokens::spaceMd);
    m_iconBox = new IconBox(header);
    head->addWidget(m_iconBox, 0, Qt::AlignTop);
    auto* texts = new QWidget(header);
    auto* textColumn = new QVBoxLayout(texts);
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(0);
    auto* name = new EllipsisLabel(texts);
    Style::applyRole(name, Role::Heading);
    m_name = name;
    textColumn->addWidget(m_name);
    auto* caption = new EllipsisLabel(texts);
    Style::applyRole(caption, Role::Caption);
    Style::setProperty(caption, "zeroTone", "secondary");
    m_caption = caption;
    textColumn->addWidget(m_caption);
    head->addWidget(texts, 1, Qt::AlignVCenter);

    m_close = new QToolButton(header);
    m_close->setObjectName(QStringLiteral("zeroDepClose"));
    Style::setProperty(m_close, "zeroVariant", "ghost");
    Style::setProperty(m_close, "zeroSize", "icon");  // no text, no padding: only the icon
    m_close->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_close->setText(QString());
    m_close->setIconSize(QSize(14, 14));
    m_close->setFixedSize(28, 28);
    m_close->setToolTip(tr("Close"));
    m_close->setAccessibleName(tr("Close"));
    m_close->setCursor(Qt::PointingHandCursor);
    m_close->setIcon(Icons::icon(QStringLiteral("x"), Theme::current().text));
    connect(m_close, &QToolButton::clicked, this, &ModDependencyPanel::closeRequested);
    head->addWidget(m_close, 0, Qt::AlignTop);
    root->addWidget(header);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    m_scroll->viewport()->setAutoFillBackground(false);
    root->addWidget(m_scroll, 1);
}

void ModDependencyPanel::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(t.border, 1));
    p.setBrush(t.surface);
    p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
}

void ModDependencyPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        emit closeRequested();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

void ModDependencyPanel::refreshIcon()
{
    auto* box = static_cast<IconBox*>(m_iconBox);
    QPixmap pixmap;
    if (!m_data.iconId.isEmpty() && m_icons) {
        pixmap = m_icons(m_data.iconId, kIcon, devicePixelRatioF());
    }
    box->setPixmap(pixmap);
}

void ModDependencyPanel::setData(const PanelData& data)
{
    m_data = data;
    static_cast<EllipsisLabel*>(m_name)->setFullText(data.name);
    const QString caption = data.version.trimmed().isEmpty() ? data.fileName : tr("%1 · %2").arg(data.version.trimmed(), data.fileName);
    static_cast<EllipsisLabel*>(m_caption)->setFullText(caption, data.path);
    refreshIcon();
    rebuild();
}

QWidget* ModDependencyPanel::addSection(const QString& title)
{
    auto* section = new QWidget(m_content);
    section->setObjectName(QStringLiteral("zeroDepSection"));
    section->setProperty("sectionTitle", title);
    auto* column = new QVBoxLayout(section);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(kRowGap);
    column->addWidget(makeLabel(title, Role::Overline, section, "secondary"));
    m_sections->addWidget(section);
    return section;
}

void ModDependencyPanel::rebuild()
{
    const int scroll = m_scroll->verticalScrollBar()->value();
    if (QWidget* old = m_scroll->takeWidget()) {
        old->hide();
        old->deleteLater();  // setWidget() would delete it at once; this may run from the click of one of its own buttons
    }
    m_content = new QWidget();
    m_content->setObjectName(QStringLiteral("zeroDepContent"));
    m_content->setAttribute(Qt::WA_TranslucentBackground);
    m_sections = new QVBoxLayout(m_content);
    m_sections->setContentsMargins(0, 0, 0, 0);
    m_sections->setSpacing(Tokens::spaceLg);
    m_scroll->setWidget(m_content);
    m_content->setAutoFillBackground(false);  // setWidget() turns it on, which paints the palette base (black) over the card

    const auto pill = [](QWidget* parent, const QString& text, PillTag::Tone tone, const QString& tip = QString()) {
        auto* tag = new PillTag(shortened(text, kPillMaxText, Style::font(Role::Caption)), tone, parent);
        if (!tip.isEmpty()) {
            tag->setToolTip(tip);
        } else if (tag->text() != text) {
            tag->setToolTip(text);
        }
        return tag;
    };
    // One row: name + caption at the left, then the state pill and its action.
    const auto addRow = [this](QWidget* section, const QString& name, const QString& caption, PillTag* tag, QPushButton* button) {
        auto* row = new QWidget(section);
        row->setObjectName(QStringLiteral("zeroDepRow"));
        row->setProperty("rowText", caption.isEmpty() ? name : QStringLiteral("%1 | %2").arg(name, caption));
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(Tokens::spaceSm);
        auto* texts = new QWidget(row);
        auto* column = new QVBoxLayout(texts);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        auto* nameLabel = new EllipsisLabel(texts);
        Style::applyRole(nameLabel, Role::Label);
        nameLabel->setFullText(name);
        column->addWidget(nameLabel);
        if (!caption.isEmpty()) {
            auto* captionLabel = new EllipsisLabel(texts);
            Style::applyRole(captionLabel, Role::Caption);
            Style::setProperty(captionLabel, "zeroTone", "secondary");
            captionLabel->setFullText(caption);
            column->addWidget(captionLabel);
        }
        line->addWidget(texts, 1);
        if (tag) {
            line->addWidget(tag, 0, Qt::AlignVCenter);
        }
        if (button) {
            line->addWidget(button, 0, Qt::AlignVCenter);
        }
        qobject_cast<QVBoxLayout*>(section->layout())->addWidget(row);
    };
    const auto addNote = [](QWidget* section, const QString& text) {
        auto* note = makeLabel(text, Role::Body, section, "secondary");
        note->setObjectName(QStringLiteral("zeroDepEmpty"));
        note->setWordWrap(true);
        qobject_cast<QVBoxLayout*>(section->layout())->addWidget(note);
    };

    const ModInspection& mod = m_data.inspection;
    if (m_data.scanning) {
        auto* skeleton = new ZeroSkeleton(ZeroSkeleton::Shape::Rows, QString(), m_content);
        skeleton->setObjectName(QStringLiteral("zeroDepSkeleton"));
        skeleton->setFixedHeight(kSkeletonLines * 66);
        m_sections->addWidget(skeleton);
    } else if (!mod.readable) {
        auto* note = makeLabel(tr("ZERO couldn't read this mod's details."), Role::Body, m_content, "secondary");
        note->setObjectName(QStringLiteral("zeroDepUnreadable"));
        note->setWordWrap(true);
        m_sections->addWidget(note);
    } else {
        // ---- Needs
        QWidget* needs = addSection(tr("Needs"));
        if (mod.needs.isEmpty()) {
            addNote(needs, tr("Needs no other mods."));
        }
        for (const DepEntry& d : mod.needs) {
            PillTag* tag = nullptr;
            QPushButton* button = nullptr;
            switch (d.state) {
                case DepState::Installed:
                    tag = pill(needs, d.version.isEmpty() ? tr("Installed") : d.version, PillTag::Tone::Success);
                    break;
                case DepState::Bundled:
                    tag = pill(needs, tr("Bundled"), PillTag::Tone::Success, tr("Inside %1").arg(d.fileName));
                    break;
                case DepState::Disabled:
                    tag = pill(needs, tr("Disabled"), PillTag::Tone::Warning, tr("In %1").arg(d.fileName));
                    button = smallButton(tr("Enable"), QStringLiteral("zeroDepEnable"), needs);
                    button->setEnabled(m_data.canChange);
                    button->setAccessibleName(tr("Enable %1").arg(d.name));
                    connect(button, &QPushButton::clicked, this, [this, path = d.path]() { emit enableRequested(path); });
                    break;
                case DepState::WrongVersion:
                    tag = pill(needs, d.version.isEmpty() ? tr("Wrong version") : tr("Has %1").arg(d.version), PillTag::Tone::Warning);
                    break;
                case DepState::Missing:
                    tag = pill(needs, tr("Missing"), PillTag::Tone::Danger);
                    button = smallButton(tr("Find"), QStringLiteral("zeroDepFind"), needs);
                    button->setAccessibleName(tr("Find %1").arg(d.name));
                    connect(button, &QPushButton::clicked, this, [this, id = d.id]() { emit findRequested(id); });
                    break;
                case DepState::Unknown:
                case DepState::NotInstalled:
                    tag = pill(needs, tr("Can't tell"), PillTag::Tone::Plain);
                    break;
            }
            addRow(needs, d.name, d.forBundled.isEmpty() ? ruleText(d.predicate) : tr("for bundled %1").arg(d.forBundled), tag, button);
        }

        // ---- Game
        if (!mod.game.isEmpty()) {
            QWidget* game = addSection(tr("Game"));
            for (const GameEntry& g : mod.game) {
                PillTag* tag = nullptr;
                if (g.kind == GameKind::Java) {
                    tag = pill(game, tr("Not checked"), PillTag::Tone::Plain);
                } else if (g.state == GameState::Ok) {
                    tag = pill(game, g.version, PillTag::Tone::Success);
                } else if (g.state == GameState::Mismatch) {
                    tag = pill(game, tr("Needs %1").arg(ruleText(g.predicate)), PillTag::Tone::Warning, tr("This instance runs %1").arg(g.version));
                } else {
                    tag = pill(game, tr("Can't tell"), PillTag::Tone::Plain);
                }
                addRow(game, g.name, ruleText(g.predicate), tag, nullptr);
            }
        }

        // ---- Optional
        if (!mod.optional.isEmpty()) {
            QWidget* optional = addSection(tr("Optional"));
            for (const DepEntry& d : mod.optional) {
                const bool installed = d.state == DepState::Installed;
                addRow(optional, d.name, ruleText(d.predicate), pill(optional, installed ? tr("Installed") : tr("Not installed"), PillTag::Tone::Plain),
                       nullptr);
            }
        }
    }

    // ---- Needed by (also for a mod ZERO could not read)
    if (!m_data.scanning) {
        QWidget* neededBy = addSection(tr("Needed by"));
        if (m_data.neededBy.isEmpty()) {
            addNote(neededBy, tr("No installed mod needs %1.").arg(m_data.name));
        }
        for (const PanelNeededBy& n : m_data.neededBy) {
            auto* button = new QPushButton(n.enabled ? n.name : tr("%1 · disabled").arg(n.name), neededBy);
            button->setObjectName(QStringLiteral("zeroDepNeededBy"));
            button->setProperty("modPath", n.path);
            Style::setProperty(button, "zeroVariant", "ghost");
            button->setStyleSheet(QStringLiteral("text-align: left;"));
            button->setCursor(Qt::PointingHandCursor);
            button->setAccessibleName(tr("Show %1").arg(n.name));
            connect(button, &QPushButton::clicked, this, [this, path = n.path]() { emit neededByClicked(path); });
            qobject_cast<QVBoxLayout*>(neededBy->layout())->addWidget(button);
        }
    }

    // ---- Conflicts: the installed ones as rows; the rest as one caption (the names in its tooltip)
    if (!m_data.scanning && m_data.inspection.readable &&
        (!m_data.inspection.conflicts.isEmpty() || !m_data.inspection.otherConflicts.isEmpty())) {
        QWidget* conflicts = addSection(tr("Conflicts"));
        for (const DepEntry& d : m_data.inspection.conflicts) {
            addRow(conflicts, d.name, d.predicate.isEmpty() || d.predicate == QLatin1String("?") ? QString() : d.predicate,
                   pill(conflicts, tr("Installed"), PillTag::Tone::Danger), nullptr);
        }
        const QStringList& others = m_data.inspection.otherConflicts;
        if (!others.isEmpty()) {
            auto* more = makeLabel(others.size() == 1 ? tr("Declares 1 more conflict with a mod that isn't installed.")
                                                       : tr("Declares %1 more conflicts with mods that aren't installed.").arg(others.size()),
                                   Role::Caption, conflicts, "secondary");
            more->setObjectName(QStringLiteral("zeroDepMoreConflicts"));
            more->setWordWrap(true);
            more->setToolTip(others.join(QStringLiteral(", ")));
            qobject_cast<QVBoxLayout*>(conflicts->layout())->addWidget(more);
        }
    }
    m_sections->addStretch(1);
    m_scroll->verticalScrollBar()->setValue(scroll);
}

// ---- parts for the tests ---------------------------------------------------------------------------------------------------------

ZeroSkeleton* ModDependencyPanel::skeleton() const
{
    return m_content ? m_content->findChild<ZeroSkeleton*>(QStringLiteral("zeroDepSkeleton")) : nullptr;
}

QStringList ModDependencyPanel::sectionTitles() const
{
    QStringList out;
    if (m_content) {
        for (const QWidget* section : m_content->findChildren<QWidget*>(QStringLiteral("zeroDepSection"))) {
            out.append(section->property("sectionTitle").toString());
        }
    }
    return out;
}

QList<QPushButton*> ModDependencyPanel::enableButtons() const
{
    return m_content ? m_content->findChildren<QPushButton*>(QStringLiteral("zeroDepEnable")) : QList<QPushButton*>();
}

QList<QPushButton*> ModDependencyPanel::findButtons() const
{
    return m_content ? m_content->findChildren<QPushButton*>(QStringLiteral("zeroDepFind")) : QList<QPushButton*>();
}

QList<QPushButton*> ModDependencyPanel::neededByButtons() const
{
    return m_content ? m_content->findChildren<QPushButton*>(QStringLiteral("zeroDepNeededBy")) : QList<QPushButton*>();
}

QStringList ModDependencyPanel::pillTexts() const
{
    QStringList out;
    if (m_content) {
        for (const PillTag* tag : m_content->findChildren<PillTag*>()) {
            out.append(tag->text());
        }
    }
    return out;
}

QStringList ModDependencyPanel::rowTexts(const QString& sectionTitle) const
{
    QStringList out;
    if (!m_content) {
        return out;
    }
    for (const QWidget* section : m_content->findChildren<QWidget*>(QStringLiteral("zeroDepSection"))) {
        if (section->property("sectionTitle").toString() != sectionTitle) {
            continue;
        }
        for (const QWidget* row : section->findChildren<QWidget*>(QStringLiteral("zeroDepRow"))) {
            out.append(row->property("rowText").toString());
        }
    }
    return out;
}

QStringList ModDependencyPanel::emptyTexts() const
{
    QStringList out;
    if (m_content) {
        for (const QLabel* label : m_content->findChildren<QLabel*>(QStringLiteral("zeroDepEmpty"))) {
            out.append(label->text());
        }
    }
    return out;
}

}  // namespace ModsUi
}  // namespace Zero
