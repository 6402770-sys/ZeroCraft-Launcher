// SPDX-License-Identifier: GPL-3.0-only
#include "AppearancePage.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>
#include "Application.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"
#include "ui/themes/ITheme.h"
#include "ui/themes/ThemeManager.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroSound.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroPixelFont.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTransition.h"
#include "ui/zero/pages/ThemeEditorPanel.h"
#include "ui/zero/pages/ThemeLibrary.h"
#include "zero/theme/ThemeArchive.h"
#include "zero/theme/ThemeEditorModel.h"

namespace Zero {

namespace {

constexpr int kContentMaxWidth = 960;
constexpr int kPreviewMaxHeight = 340;
constexpr int kStagePadding = 12;
constexpr qreal kLogicalW = 640.0;  // the miniature window is drawn in a 640x360 space and scaled
constexpr qreal kLogicalH = 360.0;

QLabel* makeLabel(const QString& text, Role role, QWidget* parent, bool secondary = false)
{
    auto* l = new QLabel(text, parent);
    Style::applyRole(l, role);
    if (secondary)
        Style::setProperty(l, "zeroTone", "secondary");
    return l;
}

/// Font for a type role of `spec` (the preview must not use the active theme's fonts).
QFont specFont(const ThemeSpec& spec, Role role, int px, int weight)
{
    QFont f;
    if (spec.usesPixelFont(role) && registerPixelFont()) {
        f.setFamilies({ pixelFamily() });
        f.setPixelSize(qMax(10, int(qRound(px / 10.0)) * 10));
        f.setWeight(QFont::Normal);
        f.setHintingPreference(QFont::PreferNoHinting);
        f.setKerning(false);
        f.setStyleStrategy(QFont::NoAntialias);
        return f;
    }
    f.setFamilies({ QLatin1String(Tokens::fontFamilyPrimary), QLatin1String(Tokens::fontFamilyFallback) });
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

/// Raised-block edges for `spec` (the bevel of its buttons): 1 px light top, 2 px dark bottom, inside `r`.
void paintSpecBevel(QPainter& p, const QRectF& r, const ThemeSpec& spec)
{
    if (!spec.bevel)
        return;
    p.save();
    p.setClipPath(Paint::panelPath(r, spec.radiusMd, spec.corners));
    p.fillRect(QRectF(r.left(), r.top(), r.width(), 1.0), withAlpha(Qt::white, Tokens::bevelLightAlpha * 2.0));
    p.fillRect(QRectF(r.left(), r.bottom() - 2.0, r.width(), 2.0), withAlpha(spec.bevelDark, 0.6));
    p.restore();
}

void fillTextured(QPainter& p, const QRectF& r, const QColor& base, const ThemeSpec& spec)
{
    if (spec.texture)
        p.drawTiledPixmap(r.toAlignedRect(), Style::texture(spec, base));
    else
        p.fillRect(r, base);
}

/// Paints a miniature ZERO window for `spec` inside `window` (any size, 16:9). Drawn in a 640x360 logical space.
void paintMiniWindow(QPainter& p, const QRectF& window, const ThemeSpec& spec)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(window.topLeft());
    const qreal k = window.width() / kLogicalW;
    p.scale(k, k);

    const QRectF win(0.5, 0.5, kLogicalW - 1.0, kLogicalH - 1.0);
    const QPainterPath winPath = Paint::panelPath(win, spec.radiusLg, spec.corners);
    p.setClipPath(winPath);
    fillTextured(p, win, spec.bg, spec);

    // rail
    const QRectF rail(0, 0, 140, kLogicalH);
    fillTextured(p, rail, spec.surface, spec);
    p.fillRect(QRectF(rail.right() - 1, 0, 1, kLogicalH), spec.border);
    p.setPen(spec.text);
    p.setFont(specFont(spec, Role::Title, 20, 600));
    p.drawText(QRectF(16, 14, 110, 26), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("ZERO"));
    for (int i = 0; i < 3; ++i) {
        const QRectF item(10, 62 + i * 34, rail.width() - 20, 28);
        if (i == 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(spec.elevated);
            p.drawPath(Paint::panelPath(item, spec.radiusMd, spec.corners));
            p.setBrush(spec.accent);
            p.drawRect(QRectF(item.left() - 10, item.top() + 5, 2, item.height() - 10));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(i == 0 ? spec.text : spec.textTertiary);
        p.drawRoundedRect(QRectF(item.left() + 10, item.center().y() - 5, 10, 10), 2, 2);
        p.setBrush(i == 0 ? spec.text : spec.textSecondary);
        p.drawRoundedRect(QRectF(item.left() + 28, item.center().y() - 2.5, 40 + (i == 1 ? 8 : 0), 5), 2.5, 2.5);
    }

    // page header
    p.setPen(spec.text);
    p.setFont(specFont(spec, Role::Title, 20, 600));
    p.drawText(QRectF(rail.right() + 24, 14, 200, 28), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Home"));

    // hero card
    const QRectF hero(rail.right() + 24, 56, kLogicalW - rail.right() - 48, 150);
    p.setBrush(spec.surface);
    p.setPen(QPen(spec.border, 1));
    p.drawPath(Paint::panelPath(hero.adjusted(0.5, 0.5, -0.5, -0.5), spec.radiusLg, spec.corners));
    const QRectF tile(hero.left() + 18, hero.top() + 18, 114, 114);
    p.setPen(Qt::NoPen);
    p.setBrush(spec.elevated);
    p.drawPath(Paint::panelPath(tile, spec.radiusLg, spec.corners));
    p.setBrush(spec.accent2);
    p.drawPath(Paint::panelPath(QRectF(tile.center().x() - 24, tile.center().y() - 24, 48, 48), spec.radiusMd, spec.corners));
    const qreal tx = tile.right() + 22;
    p.setBrush(spec.textTertiary);
    p.drawRoundedRect(QRectF(tx, hero.top() + 22, 60, 6), 3, 3);
    p.setBrush(spec.text);
    p.drawRoundedRect(QRectF(tx, hero.top() + 40, 170, 12), 3, 3);
    p.setBrush(spec.textSecondary);
    p.drawRoundedRect(QRectF(tx, hero.top() + 62, 120, 6), 3, 3);
    const QRectF btn(tx, hero.bottom() - 52, 112, 34);
    p.setBrush(spec.accent);
    p.drawPath(Paint::panelPath(btn, spec.radiusMd, spec.corners));
    paintSpecBevel(p, btn, spec);
    p.setPen(spec.onAccent);
    p.setFont(specFont(spec, Role::Label, 14, 600));
    p.drawText(btn, Qt::AlignCenter, QStringLiteral("Play"));
    p.setBrush(spec.elevated);
    p.setPen(QPen(spec.border, 1));
    p.drawPath(Paint::panelPath(QRectF(btn.right() + 10, btn.top(), 64, 34), spec.radiusMd, spec.corners));

    // two small cards
    const qreal cw = (hero.width() - 16) / 2.0;
    for (int i = 0; i < 2; ++i) {
        const QRectF card(hero.left() + i * (cw + 16), hero.bottom() + 16, cw, 64);
        p.setBrush(spec.surface);
        p.setPen(QPen(spec.border, 1));
        p.drawPath(Paint::panelPath(card.adjusted(0.5, 0.5, -0.5, -0.5), spec.radiusLg, spec.corners));
        p.setPen(Qt::NoPen);
        p.setBrush(spec.elevated);
        p.drawPath(Paint::panelPath(QRectF(card.left() + 12, card.top() + 14, 36, 36), spec.radiusMd, spec.corners));
        p.setBrush(spec.text);
        p.drawRoundedRect(QRectF(card.left() + 60, card.top() + 20, 80, 8), 4, 4);
        p.setBrush(spec.textTertiary);
        p.drawRoundedRect(QRectF(card.left() + 60, card.top() + 38, 54, 6), 3, 3);
    }

    p.setClipping(false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(spec.borderStrong, 1));
    p.drawPath(winPath);
    p.restore();
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------

/// Miniature ZERO window painted from a ThemeSpec; crossfades when the spec changes.
class ThemePreview : public QWidget {
   public:
    explicit ThemePreview(QWidget* parent) : QWidget(parent), m_anim(this)
    {
        m_from = m_to = Theme::current();
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_anim.setStartValue(0.0);
        m_anim.setEndValue(1.0);
        m_anim.setEasingCurve(Motion::easeEnter());
        connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_t = v.toReal();
            update();
        });
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

    /// `force`: repaint even when the id is the one shown (an edited theme keeps its id).
    void setSpec(const ThemeSpec& spec, bool animate, bool force = false)
    {
        if (!force && spec.id == m_to.id && m_t >= 1.0)
            return;
        m_anim.stop();
        const int ms = animate ? Motion::duration(Tokens::durBase) : 0;
        if (ms <= 0) {
            m_from = m_to = spec;
            m_t = 1.0;
            update();
            return;
        }
        // Start from what is on screen (a half-faded preview keeps its blend as the new "from" only approximately:
        // the target of the interrupted fade wins when it is already mostly visible).
        if (m_t >= 0.5)
            m_from = m_to;
        m_to = spec;
        m_t = 0.0;
        m_anim.setDuration(ms);
        m_anim.start();
    }

    const ThemeSpec& spec() const { return m_to; }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return qMin(kPreviewMaxHeight, w * 9 / 16); }
    QSize sizeHint() const override { return { 640, heightForWidth(640) }; }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF stage = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(t.surface);
        p.setPen(QPen(t.border, 1));
        p.drawPath(Paint::panelPath(stage, t.radiusLg));

        const QRectF avail = stage.adjusted(kStagePadding, kStagePadding, -kStagePadding, -kStagePadding);
        const qreal k = qMin(avail.width() / kLogicalW, avail.height() / kLogicalH);
        const QRectF window(avail.center().x() - kLogicalW * k / 2.0, avail.center().y() - kLogicalH * k / 2.0, kLogicalW * k, kLogicalH * k);
        paintMiniWindow(p, window, m_from);
        if (m_t > 0.0) {
            p.setOpacity(m_t);
            paintMiniWindow(p, window, m_to);
        }
    }

   private:
    ThemeSpec m_from, m_to;
    qreal m_t = 1.0;
    QVariantAnimation m_anim;
};

/// Four swatches of a ThemeSpec: bg, surface, accent, accent2.
class SwatchRow : public QWidget {
   public:
    SwatchRow(const ThemeSpec& spec, QWidget* parent) : QWidget(parent), m_spec(spec)
    {
        setFixedHeight(24);
        setMinimumWidth(4 * 14 + 3 * 4);  // narrower cards (the editor panel is open) squeeze the swatches
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override { return { 4 * 32 + 3 * 6, 24 }; }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor colors[4] = { m_spec.bg, m_spec.surfaceSolid(), m_spec.accent, m_spec.accent2 };
        const qreal gap = width() >= 4 * 32 + 3 * 6 ? 6.0 : 4.0;
        const qreal w = std::min<qreal>(32.0, (width() - 3 * gap) / 4.0);
        qreal x = 0;
        for (const QColor& c : colors) {
            const QRectF r(x + 0.5, 0.5, w - 1, 23);
            p.setBrush(c);
            p.setPen(QPen(m_spec.borderStrong, 1));
            p.drawPath(Paint::panelPath(r, m_spec.radiusSm, m_spec.corners));
            x += w + gap;
        }
    }

   private:
    ThemeSpec m_spec;
};

/// Selectable card for one theme: swatches, name, tooltip text and a "Current" badge.
class ThemeCard : public ZeroCard {
   public:
    ThemeCard(const QString& id, const ThemeSpec& spec, const QString& name, const QString& description, bool custom, QWidget* parent)
        : ZeroCard(parent), m_id(id), m_spec(spec)
    {
        setClickable(true);
        setMinimumHeight(112);
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
        lay->setSpacing(Tokens::spaceSm);
        // The top row has the same height on every card (the options button of a custom theme is taller than the swatches), so
        // the names line up across a row of cards.
        auto* topRow = new QWidget(this);
        topRow->setFixedHeight(28);
        auto* top = new QHBoxLayout(topRow);
        top->setContentsMargins(0, 0, 0, 0);
        top->addWidget(new SwatchRow(spec, topRow), 0, Qt::AlignLeft | Qt::AlignVCenter);
        top->addStretch(1);
        if (custom) {
            // Options of a user-made theme (Edit, Duplicate, Export, Delete); keyboard reachable, left of the "Current" badge.
            m_menu = new QToolButton(topRow);
            Style::setProperty(m_menu, "zeroVariant", "ghost");
            Style::setProperty(m_menu, "zeroSize", "icon");
            m_menu->setToolButtonStyle(Qt::ToolButtonIconOnly);
            m_menu->setIconSize(QSize(16, 16));
            m_menu->setFixedSize(28, 28);
            m_menu->setCursor(Qt::PointingHandCursor);
            m_menu->setToolTip(tr("Theme options"));
            m_menu->setAccessibleName(tr("Theme options for %1").arg(name));
            const auto tint = [this]() { m_menu->setIcon(Icons::icon(QStringLiteral("ellipsis"), Theme::current().textSecondary)); };
            tint();
            connect(&Theme::instance(), &Theme::changed, m_menu, tint);
            top->addWidget(m_menu);
        }
        m_badge = new QLabel(tr("Current"), topRow);
        Style::setProperty(m_badge, "zeroBadge", "neutral");
        m_badge->hide();
        top->addWidget(m_badge);
        lay->addWidget(topRow);
        lay->addWidget(makeLabel(name, Role::Heading, this));
        auto* desc = makeLabel(description, Role::Caption, this, true);
        desc->setWordWrap(true);
        lay->addWidget(desc);
        lay->addStretch(1);
        setToolTip(description);
        setAccessibleName(name);
    }

    QString id() const { return m_id; }
    const ThemeSpec& spec() const { return m_spec; }
    /// The "..." options button; null for the built-in themes.
    QToolButton* menuButton() const { return m_menu; }
    void setCurrent(bool current) { m_badge->setVisible(current); }
    void setSelected(bool selected)
    {
        m_selected = selected;
        update();
    }

   protected:
    void paintEvent(QPaintEvent* e) override
    {
        ZeroCard::paintEvent(e);
        if (!m_selected)
            return;
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.accent2, 2));
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusLg));
    }

   private:
    QString m_id;
    ThemeSpec m_spec;
    QLabel* m_badge;
    QToolButton* m_menu = nullptr;
    bool m_selected = false;
};

// ---------------------------------------------------------------------------------------------------------------------

AppearancePage::AppearancePage(QWidget* parent) : ShellPage(parent)
{
    auto* pageLay = new QHBoxLayout(this);  // [page | theme editor panel]
    pageLay->setContentsMargins(0, 0, 0, 0);
    pageLay->setSpacing(0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inner = new QWidget;
    scroll->setWidget(inner);
    inner->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    pageLay->addWidget(scroll, 1);

    auto* outerRow = new QHBoxLayout(inner);
    outerRow->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
    outerRow->addStretch(1);
    auto* column = new QWidget(inner);
    column->setMaximumWidth(kContentMaxWidth);
    outerRow->addWidget(column, 1000);
    outerRow->addStretch(1);

    auto* col = new QVBoxLayout(column);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // ----- Theme -----
    auto* themeHead = new QHBoxLayout;
    themeHead->setContentsMargins(0, 0, 0, 0);
    themeHead->setSpacing(Tokens::spaceSm);
    themeHead->addWidget(makeLabel(tr("Theme"), Role::Heading, column), 1);
    auto* createButton = new QPushButton(tr("Create theme"), column);
    Style::setProperty(createButton, "zeroVariant", "secondary");
    createButton->setCursor(Qt::PointingHandCursor);
    createButton->setAccessibleName(tr("Create theme"));
    themeHead->addWidget(createButton);
    auto* importButton = new QPushButton(tr("Import\u2026"), column);
    Style::setProperty(importButton, "zeroVariant", "ghost");
    importButton->setCursor(Qt::PointingHandCursor);
    importButton->setAccessibleName(tr("Import theme"));
    themeHead->addWidget(importButton);
    col->addLayout(themeHead);
    col->addSpacing(Tokens::spaceMd);
    m_preview = new ThemePreview(column);
    col->addWidget(m_preview);
    col->addSpacing(Tokens::spaceLg);
    m_cardRow = new QWidget(column);
    m_cardGrid = new QGridLayout(m_cardRow);
    m_cardGrid->setContentsMargins(0, 0, 0, 0);
    m_cardGrid->setSpacing(Tokens::spaceLg);
    for (int c = 0; c < 3; ++c)
        m_cardGrid->setColumnStretch(c, 1);
    col->addWidget(m_cardRow);
    col->addSpacing(Tokens::spaceLg);

    auto* applyRow = new QHBoxLayout;
    applyRow->setContentsMargins(0, 0, 0, 0);
    applyRow->addStretch(1);
    m_apply = new QPushButton(tr("Apply theme"), column);
    Style::setProperty(m_apply, "zeroVariant", "primary");
    connect(m_apply, &QPushButton::clicked, this, [this]() {
        if (m_editor && m_editor->isOpen())
            m_editor->cancel();  // the editor's live look would otherwise fight the theme being applied
        if (!m_selectedId.isEmpty() && m_selectedId != m_currentId)
            emit applyThemeRequested(m_selectedId);
    });
    applyRow->addWidget(m_apply);
    col->addLayout(applyRow);
    col->addSpacing(Tokens::spaceXxl);

    // ----- Motion -----
    col->addWidget(makeLabel(tr("Motion"), Role::Heading, column));
    col->addSpacing(Tokens::spaceMd);
    auto* motionRow = new QHBoxLayout;
    motionRow->setContentsMargins(0, 0, 0, 0);
    motionRow->setSpacing(Tokens::spaceXs);
    m_motionGroup = new QButtonGroup(this);
    m_motionGroup->setExclusive(true);
    const struct {
        const char* value;
        QString label;
    } options[] = { { "on", tr("On") }, { "system", tr("Follow Windows") }, { "off", tr("Off") } };
    for (const auto& o : options) {
        auto* b = new QPushButton(o.label, column);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        Style::setProperty(b, "zeroVariant", "ghost");
        b->setProperty("zeroMotionValue", QString::fromLatin1(o.value));
        m_motionGroup->addButton(b);
        motionRow->addWidget(b);
    }
    motionRow->addSpacing(Tokens::spaceMd);
    auto* previewBtn = new QPushButton(tr("Preview"), column);
    Style::setProperty(previewBtn, "zeroVariant", "ghost");
    previewBtn->setToolTip(tr("Replays a transition on the theme preview"));
    connect(previewBtn, &QPushButton::clicked, this,
            [this]() { Transition::run(m_preview, Transition::Kind::Rise, 0, [this]() { m_preview->update(); }); });
    motionRow->addWidget(previewBtn);
    motionRow->addStretch(1);
    col->addLayout(motionRow);
    col->addSpacing(Tokens::spaceSm);
    m_motionCaption = makeLabel({}, Role::Caption, column, true);
    m_motionCaption->setWordWrap(true);
    col->addWidget(m_motionCaption);
    col->addSpacing(Tokens::spaceXxl);

    connect(m_motionGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton* b) {
        APPLICATION->settings()->set("ZeroMotion", b->property("zeroMotionValue").toString());
        syncMotion();
    });

    // ----- Interface sounds -----
    col->addWidget(makeLabel(tr("Interface sounds"), Role::Heading, column));
    col->addSpacing(Tokens::spaceMd);
    auto* soundsRow = new QHBoxLayout;
    soundsRow->setContentsMargins(0, 0, 0, 0);
    soundsRow->setSpacing(Tokens::spaceMd);
    m_soundsCheck = new QCheckBox(tr("Interface sounds"), column);
    m_soundsCheck->setAccessibleName(tr("Interface sounds"));
    soundsRow->addWidget(m_soundsCheck);
    soundsRow->addStretch(1);
    m_soundsPreview = new QPushButton(tr("Preview"), column);
    Style::setProperty(m_soundsPreview, "zeroVariant", "ghost");
    m_soundsPreview->setToolTip(tr("Plays a click and a success sound with the current theme"));
    connect(m_soundsPreview, &QPushButton::clicked, this, []() {
        Sound::preview(Sound::Event::Click);
        QTimer::singleShot(220, qApp, []() { Sound::preview(Sound::Event::Success); });
    });
    soundsRow->addWidget(m_soundsPreview);
    col->addLayout(soundsRow);
    col->addSpacing(Tokens::spaceSm);
    m_soundsCaption = makeLabel(tr("Short clicks and chimes when you press buttons, switch options, change sections or finish something. "
                                   "The style follows the theme: Minecraft plays retro 8-bit blips, the other themes play soft clicks."),
                                Role::Caption, column, true);
    m_soundsCaption->setWordWrap(true);
    col->addWidget(m_soundsCaption);
    col->addSpacing(Tokens::spaceMd);
    auto* volumeLine = new QHBoxLayout;
    volumeLine->setContentsMargins(0, 0, 0, 0);
    m_soundsVolumeLabel = makeLabel(tr("Volume"), Role::Body, column);
    volumeLine->addWidget(m_soundsVolumeLabel, 1);
    m_soundsValue = makeLabel(QString(), Role::Caption, column);
    volumeLine->addWidget(m_soundsValue);
    col->addLayout(volumeLine);
    col->addSpacing(Tokens::spaceXs);
    m_soundsVolume = new QSlider(Qt::Horizontal, column);
    m_soundsVolume->setRange(0, 100);
    m_soundsVolume->setAccessibleName(tr("Interface sound volume"));
    col->addWidget(m_soundsVolume);
    col->addSpacing(Tokens::spaceXxl);

    connect(m_soundsCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncingSounds)
            APPLICATION->settings()->set(Sound::kEnabledSetting, on);
    });
    connect(m_soundsVolume, &QSlider::valueChanged, this, [this](int v) {
        m_soundsValue->setText(QStringLiteral("%1%").arg(v));
        if (!m_syncingSounds)
            APPLICATION->settings()->set(Sound::kVolumeSetting, v);
    });
    connect(m_soundsVolume, &QSlider::sliderReleased, this, []() { Sound::preview(Sound::Event::Click); });

    auto* more = new QPushButton(tr("More appearance options"), column);
    Style::setProperty(more, "zeroVariant", "ghost");
    more->setCursor(Qt::PointingHandCursor);
    connect(more, &QPushButton::clicked, this, &AppearancePage::moreOptionsRequested);
    auto* moreRow = new QHBoxLayout;
    moreRow->setContentsMargins(0, 0, 0, 0);
    moreRow->addWidget(more);
    moreRow->addStretch(1);
    col->addLayout(moreRow);
    col->addStretch(1);

    // Custom themes: the editor panel (right of the page) and the file actions behind it.
    auto* themes = APPLICATION->themeManager();
    ThemeLibrary::Hooks hooks;
    hooks.themesRoot = themes->getApplicationThemesFolder().absolutePath();
    hooks.loadTheme = [](const QString& folder) { return APPLICATION->themeManager()->loadCustomTheme(folder); };
    hooks.removeTheme = [](const QString& id) { APPLICATION->themeManager()->removeCustomTheme(id); };
    hooks.applyTheme = [this](const QString& id) { emit applyThemeRequested(id); };
    hooks.reapplyTheme = [](const QString& id) {
        APPLICATION->themeManager()->setApplicationTheme(id.isEmpty() ? QString::fromLatin1(kDefaultThemeId) : id, false);
    };
    hooks.activeThemeId = [this]() { return currentThemeId(); };
    m_library = new ThemeLibrary(std::move(hooks), this, this);
    m_editor = new ThemeEditorPanel(m_library, this);
    pageLay->addWidget(m_editor);
    connect(m_library, &ThemeLibrary::themesChanged, this, [this](const QString& select) { buildThemeCards(select); });
    connect(m_editor, &ThemeEditorPanel::previewed, this, [this](const ThemeSpec& spec) { m_preview->setSpec(spec, false, true); });
    connect(m_editor, &ThemeEditorPanel::openChanged, this, [this](bool open) {
        refreshRings();
        if (!open)
            showSelectedInPreview();  // back to the selected card's theme (after Save: the saved theme)
    });
    connect(createButton, &QPushButton::clicked, this, [this]() { m_editor->openNew(); });
    connect(importButton, &QPushButton::clicked, this, [this]() { m_library->importWithDialog(); });

    buildThemeCards();
    syncMotion();
    syncSounds();
    for (const char* key : { Sound::kEnabledSetting, Sound::kVolumeSetting }) {
        if (auto setting = APPLICATION->settings()->getSetting(key))
            connect(setting.get(), &Setting::SettingChanged, this, [this](const Setting&, const QVariant&) { syncSounds(); });
    }

    // Keep in sync with the theme changing elsewhere (Prism's settings dialog, the legacy menu) and with the setting.
    connect(&Theme::instance(), &Theme::changed, this, [this]() { QTimer::singleShot(0, this, &AppearancePage::syncCurrent); });
    if (auto setting = APPLICATION->settings()->getSetting("ApplicationTheme"))
        connect(setting.get(), &Setting::SettingChanged, this, [this](const Setting&, const QVariant&) { syncCurrent(); });
    for (const char* key : { "ZeroMotion", "ZeroReduceMotion" }) {
        if (auto setting = APPLICATION->settings()->getSetting(key))
            connect(setting.get(), &Setting::SettingChanged, this, [this](const Setting&, const QVariant&) { syncMotion(); });
    }
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        if (s == Qt::ApplicationActive)
            syncMotion();  // the user may have flipped Windows "Animation effects"
    });
}

QString AppearancePage::currentThemeId() const
{
    const QString id = APPLICATION->settings()->get("ApplicationTheme").toString();
    return id.isEmpty() ? QString::fromLatin1(kDefaultThemeId) : id;  // empty = ThemeManager's ZERO default
}

void AppearancePage::buildThemeCards(const QString& select)
{
    const QString previous = m_selectedId;
    for (auto* c : std::as_const(m_cards)) {
        m_cardGrid->removeWidget(c);
        c->hide();
        c->deleteLater();
    }
    m_cards.clear();
    m_currentId = currentThemeId();

    // The built-in themes first, then the user's own by name.
    QList<ITheme*> themes = APPLICATION->themeManager()->getValidApplicationThemes();
    std::stable_sort(themes.begin(), themes.end(), [](ITheme* a, ITheme* b) {
        const bool ca = Custom::isCustomThemeId(a->id()), cb = Custom::isCustomThemeId(b->id());
        if (ca != cb)
            return !ca;
        return ca && a->name().localeAwareCompare(b->name()) < 0;
    });
    int index = 0;
    for (ITheme* theme : std::as_const(themes)) {
        ThemeSpec spec;
        if (!isZeroThemeId(theme->id()) || !specForThemeId(theme->id(), &spec))
            continue;
        const QString id = theme->id();
        auto* card = new ThemeCard(id, spec, theme->name(), theme->tooltip(), Custom::isCustomThemeId(id), m_cardRow);
        connect(card, &ZeroCard::clicked, this, [this, id]() { selectTheme(id); });
        if (QToolButton* menu = card->menuButton())
            connect(menu, &QToolButton::clicked, this, [this, id, menu]() { showThemeMenu(id, menu); });
        m_cardGrid->addWidget(card, index / 3, index % 3);
        m_cards << card;
        ++index;
    }
    // Selection: what was asked for, else what was selected, else the applied theme (or the first card).
    const auto has = [this](const QString& id) {
        return !id.isEmpty() && std::any_of(m_cards.cbegin(), m_cards.cend(), [&id](ThemeCard* c) { return c->id() == id; });
    };
    QString start = has(select) ? select : has(previous) ? previous : m_currentId;
    if (!has(start) && !m_cards.isEmpty())
        start = m_cards.first()->id();
    m_selectedId.clear();
    selectTheme(start);
    for (auto* c : std::as_const(m_cards)) {
        c->setCurrent(c->id() == m_currentId);
        if (c->id() == m_selectedId && !(m_editor && m_editor->isOpen()))
            m_preview->setSpec(c->spec(), false, true);  // an edited theme keeps its id but not its look
    }
    refreshRings();
    refreshApplyButton();
}

void AppearancePage::showThemeMenu(const QString& themeId, QWidget* anchor)
{
    QMenu menu(this);
    const auto& t = Theme::current();
    auto* edit = menu.addAction(Icons::icon(QStringLiteral("pencil"), t.text), tr("Edit"));
    auto* duplicate = menu.addAction(Icons::icon(QStringLiteral("copy"), t.text), tr("Duplicate"));
    auto* exportAction = menu.addAction(Icons::icon(QStringLiteral("upload"), t.text), tr("Export…"));
    menu.addSeparator();
    auto* remove = menu.addAction(Icons::icon(QStringLiteral("trash-2"), t.danger), tr("Delete"));
    const QAction* chosen = menu.exec(anchor->mapToGlobal(QPoint(anchor->width() - menu.sizeHint().width(), anchor->height() + 4)));
    if (chosen == edit) {
        m_editor->openEdit(themeId);
    } else if (chosen == duplicate) {
        m_library->duplicateTheme(themeId);
    } else if (chosen == exportAction) {
        m_library->exportThemeWithDialog(themeId);
    } else if (chosen == remove) {
        m_library->deleteTheme(themeId);
    }
}

void AppearancePage::paintThemeMenu(QPixmap* shot, QWidget* window, const QString& themeId)
{
    for (auto* c : std::as_const(m_cards)) {
        if (c->id() != themeId || !c->menuButton())
            continue;
        QMenu menu(this);
        const auto& t = Theme::current();
        menu.addAction(Icons::icon(QStringLiteral("pencil"), t.text), tr("Edit"));
        menu.addAction(Icons::icon(QStringLiteral("copy"), t.text), tr("Duplicate"));
        menu.addAction(Icons::icon(QStringLiteral("upload"), t.text), tr("Export…"));
        menu.addSeparator();
        menu.addAction(Icons::icon(QStringLiteral("trash-2"), t.danger), tr("Delete"));
        menu.ensurePolished();
        menu.resize(menu.sizeHint());
        const QPixmap picture = menu.grab();
        QToolButton* anchor = c->menuButton();
        const QPoint at = anchor->mapTo(window, QPoint(anchor->width() - menu.width(), anchor->height() + 4));
        QPainter p(shot);
        p.drawPixmap(at, picture);
        return;
    }
}

void AppearancePage::openThemeEditor(const QString& target)
{
    const QStringList parts = target.split(QLatin1Char(','));
    const QString what = parts.value(0);
    if (what == QLatin1String("new"))
        m_editor->openNew();
    else if (!m_editor->openEdit(what))
        qWarning() << "ZERO_SCREENSHOT_THEME_EDITOR: unknown custom theme" << what;
    auto* model = m_editor->model();
    for (int i = 1; i < parts.size(); ++i) {
        if (parts[i] == QLatin1String("minecraft"))
            model->setBase(Custom::Base::Minecraft);
        else if (parts[i] == QLatin1String("hard"))
            model->setColor(Custom::MainColor::Text, model->color(Custom::MainColor::Background).lighter(135));  // barely above the background
        else if (parts[i] == QLatin1String("all"))
            m_editor->setAllColorsExpanded(true);
    }
    m_editor->flushPreview();
}

void AppearancePage::resizeEvent(QResizeEvent* event)
{
    ShellPage::resizeEvent(event);
    if (m_editor)
        m_editor->fitTo(event->size().width());
}

void AppearancePage::selectTheme(const QString& id)
{
    if (id == m_selectedId)
        return;
    for (auto* c : std::as_const(m_cards)) {
        if (c->id() != id)
            continue;
        // While the editor is open the big preview shows the theme being edited, not this card.
        if (!(m_editor && m_editor->isOpen()))
            m_preview->setSpec(c->spec(), !m_selectedId.isEmpty());
        m_selectedId = id;
    }
    refreshRings();
    refreshApplyButton();
}

void AppearancePage::refreshRings()
{
    // The selection ring is on the selected card; while the editor is open it is on the card of the theme being edited (none for a
    // new theme). The "Current" badge is separate: it always marks the applied theme.
    const bool editing = m_editor && m_editor->isOpen();
    const QString ring = editing ? m_editor->editingThemeId() : m_selectedId;
    for (auto* c : std::as_const(m_cards))
        c->setSelected(!ring.isEmpty() && c->id() == ring);
}

void AppearancePage::showSelectedInPreview()
{
    for (auto* c : std::as_const(m_cards)) {
        if (c->id() == m_selectedId)
            m_preview->setSpec(c->spec(), true, true);
    }
}

void AppearancePage::syncCurrent()
{
    const QString now = currentThemeId();
    if (now == m_currentId)
        return;
    m_currentId = now;
    for (auto* c : std::as_const(m_cards))
        c->setCurrent(c->id() == m_currentId);
    refreshApplyButton();
}

void AppearancePage::refreshApplyButton()
{
    m_apply->setEnabled(!m_selectedId.isEmpty() && m_selectedId != m_currentId);
}

void AppearancePage::syncSounds()
{
    auto* settings = APPLICATION->settings();
    const bool on = settings->get(Sound::kEnabledSetting).toBool();
    const int volume = std::clamp(settings->get(Sound::kVolumeSetting).toInt(), 0, 100);
    m_syncingSounds = true;
    m_soundsCheck->setChecked(on);
    m_soundsVolume->setValue(volume);
    m_syncingSounds = false;
    m_soundsValue->setText(QStringLiteral("%1%").arg(volume));
    m_soundsVolume->setEnabled(on);
    m_soundsVolumeLabel->setEnabled(on);
    m_soundsValue->setEnabled(on);
}

void AppearancePage::syncMotion()
{
    auto* settings = APPLICATION->settings();
    QString value = settings->get("ZeroMotion").toString();
    if (value != QLatin1String("on") && value != QLatin1String("off") && value != QLatin1String("system"))
        value = settings->get("ZeroReduceMotion").toBool() ? QStringLiteral("off") : QStringLiteral("on");  // never chosen: on
    for (auto* b : m_motionGroup->buttons())
        b->setChecked(b->property("zeroMotionValue").toString() == value);

    if (value == QLatin1String("on")) {
        m_motionCaption->setText(tr("Pages, cards, lists and pictures animate in. Turn this off if movement bothers you."));
    } else if (value == QLatin1String("off")) {
        m_motionCaption->setText(tr("Everything changes instantly. Activity indicators, like the launch sweep, still run."));
    } else if (Motion::systemAnimationsEnabled()) {
        m_motionCaption->setText(tr("Same as Windows: animation effects are on there, so ZERO animates."));
    } else {
        m_motionCaption->setText(tr("Same as Windows: animation effects are off there, so ZERO stays still. Choose On to animate anyway."));
    }
}

}  // namespace Zero
