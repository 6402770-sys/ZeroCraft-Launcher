// SPDX-License-Identifier: GPL-3.0-only
#include "SandboxBanner.h"

#include <QLabel>
#include <QPainter>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "ui/zero/ZeroCompactButton.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/content/ContentDraw.h"
#include "ui/zero/pages/ElidedLabel.h"
#include "zero/sandbox/Sandbox.h"

namespace Zero {
namespace SandboxUi {

namespace {
constexpr int kIconSize = 16;
constexpr int kCompactButtonHeight = CompactButton::kCompactButtonHeight;
}

Requests* Requests::instance()
{
    static Requests* requests = new Requests();  // lives for the whole process
    return requests;
}

SandboxBanner::SandboxBanner(Look look, QWidget* parent) : QWidget(parent), m_look(look)
{
    setObjectName(QStringLiteral("zeroSandboxBanner"));
    const bool card = look == Look::Card;
    // Card (Library details, 300 px): title over full-width buttons. Compact (the instance window's side column, 220 px): icon + one-line
    // title, then the two small buttons one under the other at full width; it never asks for more width than the column has.
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(Tokens::spaceMd, card ? Tokens::spaceMd : Tokens::spaceSm, Tokens::spaceMd,
                               card ? Tokens::spaceMd : Tokens::spaceSm);
    column->setSpacing(Tokens::spaceSm);

    if (card) {
        // the icon is painted on the title's first line, in the room this margin leaves
        m_title = new QLabel(this);
        m_title->setWordWrap(true);
        m_title->setContentsMargins(kIconSize + Tokens::spaceSm, 0, 0, 0);
    } else {
        // first row: icon | title (one line, elided to the room there is; ElidedLabel asks for no width), on the row's centre line
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(Tokens::spaceSm);
        m_icon = new QLabel(this);
        m_icon->setObjectName(QStringLiteral("zeroSandboxIcon"));
        m_icon->setFixedSize(kIconSize, kIconSize);
        row->addWidget(m_icon, 0, Qt::AlignVCenter);
        m_title = new ElidedLabel(this);
        m_title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        row->addWidget(m_title, 1, Qt::AlignVCenter);
        column->addLayout(row);
    }
    m_title->setObjectName(QStringLiteral("zeroSandboxTitle"));
    Style::applyRole(m_title, card ? Role::Label : Role::Caption);
    Style::setProperty(m_title, "zeroTone", "info");
    m_title->setTextFormat(Qt::PlainText);
    if (card)
        column->addWidget(m_title);

    auto makeButton = [&](const char* name, const char* variant) {
        auto* button = card ? new QPushButton(this) : new CompactButton(this);
        button->setObjectName(QLatin1String(name));
        Style::setProperty(button, "zeroVariant", variant);
        if (!card) {
            Style::setProperty(button, "zeroSize", "sm");
            button->setFixedHeight(kCompactButtonHeight);
            button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // full width of the column, never wider than it
        }
        button->setAutoDefault(false);
        column->addWidget(button);
        return button;
    };
    m_bringBack = makeButton("zeroSandboxBringBack", "primary");
    m_bringBack->setText(tr("Bring changes back"));
    m_keep = makeButton("zeroSandboxKeep", "primary");
    m_keep->setText(tr("Keep as a normal instance"));
    m_discard = makeButton("zeroSandboxDiscard", "secondary");
    m_discard->setText(tr("Discard copy"));
    m_keep->hide();

    connect(m_bringBack, &QPushButton::clicked, this, [this]() {
        emit bringBackClicked();
        emit Requests::instance()->bringBackRequested(m_copyId);
    });
    connect(m_keep, &QPushButton::clicked, this, [this]() {
        emit keepClicked();
        emit Requests::instance()->keepRequested(m_copyId);
    });
    connect(m_discard, &QPushButton::clicked, this, [this]() {
        emit discardClicked();
        emit Requests::instance()->discardRequested(m_copyId);
    });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        updateIcon();
        update();
    });
    updateIcon();
    hide();
}

void SandboxBanner::updateIcon()
{
    if (m_icon)
        m_icon->setPixmap(Icons::icon(QStringLiteral("git-branch"), Theme::current().info).pixmap(QSize(kIconSize, kIconSize), devicePixelRatioF()));
}

void SandboxBanner::setState(const QString& copyId, const QString& originalName, const QString& busyReason)
{
    m_copyId = copyId;
    const bool gone = originalName.isEmpty();
    m_titleText = gone ? tr("The original instance is gone") : tr("Test copy of %1").arg(originalName);
    if (m_look == Look::Compact)
        static_cast<ElidedLabel*>(m_title)->setFullText(m_titleText);
    else
        m_title->setText(m_titleText);
    setAccessibleName(m_titleText);
    m_bringBack->setVisible(!gone);
    m_keep->setVisible(gone);
    setBusyReason(busyReason);
    setVisible(!copyId.isEmpty());
}

void SandboxBanner::setBusyReason(const QString& busyReason)
{
    const bool busy = !busyReason.isEmpty();
    for (QPushButton* button : { m_bringBack, m_keep, m_discard }) {
        button->setEnabled(!busy);
        button->setToolTip(busy ? busyReason : QString());
    }
}

void SandboxBanner::refresh(BaseInstance* copy)
{
    const QString originalId = Sandbox::sandboxOf(copy);
    if (!copy || originalId.isEmpty()) {
        setState({}, {});
        return;
    }
    BaseInstance* original = APPLICATION->instances()->getInstanceById(originalId);
    setState(copy->id(), original ? original->name() : QString(), copy->isRunning() ? tr("Close the game first.") : QString());
}

void SandboxBanner::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Content::Draw::withAlpha(t.info, Tokens::statusTint * 3), 1));
    p.setBrush(Content::Draw::withAlpha(t.info, Tokens::statusTint));
    p.drawPath(Paint::panelPath(r, t.radiusMd));
    if (m_look != Look::Card)
        return;  // the compact look has its icon in the row (m_icon)
    // the icon sits on the title's first line, in the room of the title's left margin
    const int top = m_title->geometry().top() + (m_title->fontMetrics().height() - kIconSize) / 2;
    Icons::icon(QStringLiteral("git-branch"), t.info).paint(&p, QRect(m_title->geometry().left(), top, kIconSize, kIconSize));
}

}  // namespace SandboxUi
}  // namespace Zero
