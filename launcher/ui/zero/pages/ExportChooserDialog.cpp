// SPDX-License-Identifier: GPL-3.0-only
#include "ExportChooserDialog.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>

#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {

QLabel* makeLabel(const QString& text, Role role, QWidget* parent, bool secondary = false, bool wrap = true)
{
    auto* l = new QLabel(text, parent);
    Style::applyRole(l, role);
    if (secondary)
        Style::setProperty(l, "zeroTone", "secondary");
    l->setWordWrap(wrap);
    return l;
}

/// One selectable format: icon, "Title · .ext", two description lines. Selected = 2 px accent ring (like the theme cards).
class FormatCard : public ZeroCard {
   public:
    FormatCard(const QString& iconName, const QString& title, const QString& ext, const QString& line1, const QString& line2, QWidget* parent)
        : ZeroCard(parent), m_iconName(iconName)
    {
        setClickable(true);
        setFocusPolicy(Qt::NoFocus);  // the dialog owns the keyboard (arrows, 1-3, Enter)
        setFocusRingVisible(false);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(Tokens::spaceLg, Tokens::spaceMd, Tokens::spaceLg, Tokens::spaceMd);
        row->setSpacing(Tokens::spaceMd);
        m_icon = new QLabel(this);
        m_icon->setFixedSize(24, 24);
        m_icon->setAlignment(Qt::AlignCenter);
        row->addWidget(m_icon, 0, Qt::AlignTop);
        auto* col = new QVBoxLayout;
        col->setSpacing(2);
        auto* head = new QHBoxLayout;
        head->setSpacing(Tokens::spaceSm);
        head->addWidget(makeLabel(title, Role::Heading, this, false, false));
        head->addWidget(makeLabel(QStringLiteral("· %1").arg(ext), Role::Caption, this, true, false));
        head->addStretch(1);
        col->addLayout(head);
        col->addWidget(makeLabel(line1, Role::Body, this));
        m_last = makeLabel(line2, Role::Caption, this, true);
        col->addWidget(m_last);
        row->addLayout(col, 1);
        setAccessibleName(QStringLiteral("%1 (%2)").arg(title, ext));
        refreshIcon();
        connect(&Theme::instance(), &Theme::changed, this, [this]() { refreshIcon(); });
    }

    void setSelected(bool selected)
    {
        m_selected = selected;
        update();
    }
    void setLastLine(const QString& text) { m_last->setText(text); }
    std::function<void()> onDoubleClick;

   protected:
    void changeEvent(QEvent* e) override
    {
        ZeroCard::changeEvent(e);
        if (e->type() == QEvent::EnabledChange)
            refreshIcon();
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && isEnabled() && onDoubleClick)
            onDoubleClick();
    }
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
    void refreshIcon()
    {
        const auto& t = Theme::current();
        m_icon->setPixmap(Icons::icon(m_iconName, isEnabled() ? t.text : t.textTertiary).pixmap(QSize(20, 20)));
    }
    QString m_iconName;
    QLabel* m_icon;
    QLabel* m_last;
    bool m_selected = false;
};

FormatCard* asCard(ZeroCard* c)
{
    return static_cast<FormatCard*>(c);
}

}  // namespace

QString exportFormatKey(ExportFormat format)
{
    switch (format) {
        case ExportFormat::MrPack:
            return QStringLiteral("mrpack");
        case ExportFormat::CurseForge:
            return QStringLiteral("curseforge");
        case ExportFormat::Zip:
            break;
    }
    return QStringLiteral("zip");
}

ExportFormat exportFormatFromKey(const QString& key)
{
    if (key == QLatin1String("mrpack"))
        return ExportFormat::MrPack;
    if (key == QLatin1String("curseforge"))
        return ExportFormat::CurseForge;
    return ExportFormat::Zip;
}

ExportChooserDialog::ExportChooserDialog(const QString& instanceName, bool snapshot, ExportFormat initial, QWidget* parent)
    : QDialog(parent), m_snapshot(snapshot)
{
    setObjectName(QStringLiteral("ZeroExportChooserDialog"));
    setWindowTitle(tr("Export · %1").arg(instanceName));
    setMinimumWidth(560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceLg);
    root->setSpacing(Tokens::spaceMd);

    root->addWidget(makeLabel(tr("Export · %1").arg(instanceName), Role::Title, this, false, false));
    m_subtitle = makeLabel(tr("Pick how you want to save it. You choose the exact folders on the next step."), Role::Body, this, true, false);
    root->addWidget(m_subtitle);
    root->addSpacing(Tokens::spaceSm);

    auto* zip = new FormatCard(QStringLiteral("file-archive"), tr("Full copy"), QStringLiteral(".zip"),
                               tr("Everything as it is: mods, settings and worlds. Biggest file."),
                               tr("Best for a backup, or a friend who uses ZERO or Prism."), this);
    auto* mr = new FormatCard(QStringLiteral("package"), tr("Modrinth pack"), QStringLiteral(".mrpack"),
                              tr("A small file: mods are listed and downloaded by your friend, your configs go inside."),
                              tr("Best for sharing with ZERO, Prism or Modrinth App players."), this);
    auto* cf = new FormatCard(QStringLiteral("blocks"), tr("CurseForge pack"), QStringLiteral(".zip"),
                              tr("Same idea, in CurseForge's format."), tr("Best for a friend who uses the CurseForge app."), this);
    m_cards[int(ExportFormat::Zip)] = zip;
    m_cards[int(ExportFormat::MrPack)] = mr;
    m_cards[int(ExportFormat::CurseForge)] = cf;
    if (m_snapshot) {
        cf->setEnabled(false);
        cf->setLastLine(tr("CurseForge packs can't hold snapshot versions."));
    }
    for (int i = 0; i < 3; ++i) {
        auto* c = asCard(m_cards[i]);
        root->addWidget(c);
        connect(c, &ZeroCard::clicked, this, [this, i]() { setSelected(ExportFormat(i)); });
        c->onDoubleClick = [this, i]() {
            setSelected(ExportFormat(i));
            accept();
        };
    }

    root->addSpacing(Tokens::spaceSm);
    auto* info = new ZeroCard(this);
    info->setHoverEnabled(false);
    auto* infoRow = new QHBoxLayout(info);
    infoRow->setContentsMargins(Tokens::spaceLg, Tokens::spaceMd, Tokens::spaceLg, Tokens::spaceMd);
    infoRow->setSpacing(Tokens::spaceMd);
    auto* infoIcon = new QLabel(info);
    infoIcon->setFixedSize(20, 20);
    infoRow->addWidget(infoIcon, 0, Qt::AlignTop);
    auto* infoCol = new QVBoxLayout;
    infoCol->setSpacing(2);
    infoCol->addWidget(makeLabel(tr("How your friend opens it"), Role::Label, info));
    infoCol->addWidget(makeLabel(tr("ZERO: New instance › Import, or drag the file onto the window."), Role::Caption, info, true));
    infoCol->addWidget(
        makeLabel(tr("Prism: Add instance › Import. Modrinth App and CurseForge app: their own Import."), Role::Caption, info, true));
    infoRow->addLayout(infoCol, 1);
    root->addWidget(info);
    const auto tintInfo = [infoIcon]() {
        infoIcon->setPixmap(Icons::icon(QStringLiteral("info"), Theme::current().textSecondary).pixmap(QSize(16, 16)));
    };
    tintInfo();
    connect(&Theme::instance(), &Theme::changed, infoIcon, tintInfo);

    root->addSpacing(Tokens::spaceSm);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(Tokens::spaceSm);
    buttons->addStretch(1);
    m_cancel = new QPushButton(tr("Cancel"), this);
    Style::setProperty(m_cancel, "zeroVariant", "secondary");
    m_continue = new QPushButton(tr("Continue"), this);
    Style::setProperty(m_continue, "zeroVariant", "primary");
    m_continue->setDefault(true);
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_continue);
    root->addLayout(buttons);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_continue, &QPushButton::clicked, this, &QDialog::accept);

    setSelected(isAvailable(initial) ? initial : ExportFormat::Zip);
    m_continue->setFocus();
}

bool ExportChooserDialog::isAvailable(ExportFormat format) const
{
    return !(format == ExportFormat::CurseForge && m_snapshot);
}

void ExportChooserDialog::setSelected(ExportFormat format)
{
    if (!isAvailable(format))
        return;
    m_selected = format;
    for (int i = 0; i < 3; ++i)
        asCard(m_cards[i])->setSelected(ExportFormat(i) == format);
}

void ExportChooserDialog::move(int step)
{
    int i = int(m_selected);
    for (int n = 0; n < 3; ++n) {
        i = (i + step + 3) % 3;
        if (isAvailable(ExportFormat(i))) {
            setSelected(ExportFormat(i));
            return;
        }
    }
}

void ExportChooserDialog::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_Up:
        case Qt::Key_Left:
            move(-1);
            return;
        case Qt::Key_Down:
        case Qt::Key_Right:
            move(1);
            return;
        case Qt::Key_1:
        case Qt::Key_2:
        case Qt::Key_3:
            setSelected(ExportFormat(event->key() - Qt::Key_1));
            return;
        default:
            break;
    }
    QDialog::keyPressEvent(event);
}

}  // namespace Zero
