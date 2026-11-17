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
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/content/ContentDraw.h"
#include "zero/sandbox/Sandbox.h"

namespace Zero {
namespace SandboxUi {

namespace {
constexpr int kIconSize = 16;
constexpr int kCompactButtonHeight = 28;  // small buttons get their height in code (obsidian.qss, zeroSize "sm")
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
    // Card (Library details, 300 px): title over full-width buttons. Compact (instance window): one row, title left, small buttons right.
    QBoxLayout* column = card ? static_cast<QBoxLayout*>(new QVBoxLayout(this)) : static_cast<QBoxLayout*>(new QHBoxLayout(this));
    column->setContentsMargins(Tokens::spaceMd, card ? Tokens::spaceMd : Tokens::spaceSm, card ? Tokens::spaceMd : Tokens::spaceSm,
                               card ? Tokens::spaceMd : Tokens::spaceSm);
    column->setSpacing(Tokens::spaceSm);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("zeroSandboxTitle"));
    Style::applyRole(m_title, card ? Role::Label : Role::Caption);
    Style::setProperty(m_title, "zeroTone", "info");
    m_title->setWordWrap(true);
    m_title->setTextFormat(Qt::PlainText);
    m_title->setContentsMargins(kIconSize + Tokens::spaceSm, 0, 0, 0);
    column->addWidget(m_title, card ? 0 : 1, card ? Qt::Alignment() : Qt::AlignVCenter);

    auto makeButton = [&](const char* name, const char* variant) {
        auto* button = new QPushButton(this);
        button->setObjectName(QLatin1String(name));
        Style::setProperty(button, "zeroVariant", variant);
        if (!card) {
            Style::setProperty(button, "zeroSize", "sm");
            button->setFixedHeight(kCompactButtonHeight);
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
    connect(&Theme::instance(), &Theme::changed, this, [this]() { update(); });
    hide();
}

void SandboxBanner::setState(const QString& copyId, const QString& originalName, const QString& busyReason)
{
    m_copyId = copyId;
    const bool gone = originalName.isEmpty();
    m_title->setText(gone ? tr("The original instance is gone") : tr("Test copy of %1").arg(originalName));
    setAccessibleName(m_title->text());
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
    // the icon sits on the title's first line (card) or on the row's centre line (compact)
    const int top = m_look == Look::Card ? m_title->geometry().top() + (m_title->fontMetrics().height() - kIconSize) / 2
                                         : (height() - kIconSize) / 2;
    Icons::icon(QStringLiteral("git-branch"), t.info).paint(&p, QRect(Tokens::spaceMd, top, kIconSize, kIconSize));
}

}  // namespace SandboxUi
}  // namespace Zero
