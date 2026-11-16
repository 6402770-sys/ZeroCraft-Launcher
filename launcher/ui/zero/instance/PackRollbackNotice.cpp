// SPDX-License-Identifier: GPL-3.0-only
#include "PackRollbackNotice.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/content/ContentDraw.h"

namespace Zero {
namespace PacksUi {

namespace {
constexpr int kIconSize = 16;
}

PackRollbackNotice::PackRollbackNotice(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("zeroPackRollbackNotice"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(Tokens::spaceMd + kIconSize + Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm);
    row->setSpacing(Tokens::spaceSm);

    auto* text = new QVBoxLayout();
    text->setSpacing(Tokens::spaceXs);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("zeroPackRollbackTitle"));
    Style::applyRole(m_title, Role::Label);
    Style::setProperty(m_title, "zeroTone", "warning");
    text->addWidget(m_title);
    m_body = new QLabel(this);
    m_body->setObjectName(QStringLiteral("zeroPackRollbackBody"));
    Style::applyRole(m_body, Role::Caption);
    Style::setProperty(m_body, "zeroTone", "secondary");
    m_body->setWordWrap(true);
    text->addWidget(m_body);
    row->addLayout(text, 1);

    m_restoreWithout = new QPushButton(this);
    m_restoreWithout->setObjectName(QStringLiteral("zeroPackRollbackRestoreWithout"));
    Style::setProperty(m_restoreWithout, "zeroVariant", "primary");
    m_restoreWithout->setAutoDefault(false);
    row->addWidget(m_restoreWithout, 0, Qt::AlignVCenter);
    m_restoreAll = new QPushButton(this);
    m_restoreAll->setObjectName(QStringLiteral("zeroPackRollbackRestoreAll"));
    Style::setProperty(m_restoreAll, "zeroVariant", "secondary");
    m_restoreAll->setAutoDefault(false);
    row->addWidget(m_restoreAll, 0, Qt::AlignVCenter);

    m_dismiss = new QToolButton(this);
    m_dismiss->setObjectName(QStringLiteral("zeroPackRollbackDismiss"));
    Style::setProperty(m_dismiss, "zeroVariant", "ghost");
    m_dismiss->setIcon(Icons::icon(QStringLiteral("x"), Theme::current().textSecondary));
    m_dismiss->setIconSize(QSize(14, 14));
    m_dismiss->setAutoRaise(true);
    m_dismiss->setCursor(Qt::PointingHandCursor);
    m_dismiss->setToolTip(tr("Dismiss"));
    m_dismiss->setAccessibleName(tr("Dismiss"));
    row->addWidget(m_dismiss, 0, Qt::AlignTop);

    connect(m_restoreWithout, &QPushButton::clicked, this, &PackRollbackNotice::restoreWithoutRequested);
    connect(m_restoreAll, &QPushButton::clicked, this, &PackRollbackNotice::restoreAllRequested);
    connect(m_dismiss, &QToolButton::clicked, this, &PackRollbackNotice::dismissRequested);
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        m_dismiss->setIcon(Icons::icon(QStringLiteral("x"), Theme::current().textSecondary));
        update();
    });
    hide();
}

void PackRollbackNotice::setTitleAndBody(const QString& title, const QString& body)
{
    m_title->setText(title);
    m_body->setText(body);
    setAccessibleName(title);
    setAccessibleDescription(body);
}

void PackRollbackNotice::setActions(const QString& withoutText, const QString& allText)
{
    const bool hasWithout = !withoutText.isEmpty();
    m_restoreWithout->setVisible(hasWithout);
    m_restoreWithout->setText(withoutText);
    m_restoreAll->setText(allText);
    Style::setProperty(m_restoreAll, "zeroVariant", hasWithout ? "secondary" : "primary");
}

void PackRollbackNotice::setRestoreEnabled(bool enabled, const QString& reason)
{
    for (QPushButton* button : { m_restoreWithout, m_restoreAll }) {
        button->setEnabled(enabled);
        button->setToolTip(enabled ? QString() : reason);
    }
}

void PackRollbackNotice::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Content::Draw::withAlpha(t.warning, Tokens::statusTint * 3), 1));
    p.setBrush(Content::Draw::withAlpha(t.warning, Tokens::statusTint));
    p.drawPath(Paint::panelPath(r, t.radiusMd));
    Icons::icon(QStringLiteral("triangle-alert"), t.warning).paint(&p, QRect(Tokens::spaceMd, Tokens::spaceSm + 2, kIconSize, kIconSize));
}

}  // namespace PacksUi
}  // namespace Zero
