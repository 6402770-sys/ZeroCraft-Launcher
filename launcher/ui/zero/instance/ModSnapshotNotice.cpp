// SPDX-License-Identifier: GPL-3.0-only
#include "ModSnapshotNotice.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/content/ContentDraw.h"

namespace Zero {
namespace ModsUi {

namespace {
constexpr int kIconSize = 16;
}

ModSnapshotNotice::ModSnapshotNotice(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("zeroModSnapshotNotice"));
    setMinimumHeight(Tokens::controlHeight + Tokens::spaceXs);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(Tokens::spaceMd + kIconSize + Tokens::spaceSm, Tokens::spaceXs, Tokens::spaceSm, Tokens::spaceXs);
    row->setSpacing(Tokens::spaceSm);
    m_label = new QLabel(this);
    Style::applyRole(m_label, Role::Label);
    row->addWidget(m_label, 1);
    m_rollback = new QPushButton(tr("Roll back"), this);
    m_rollback->setObjectName(QStringLiteral("zeroModSnapshotRollback"));
    Style::setProperty(m_rollback, "zeroVariant", "secondary");
    m_rollback->setAutoDefault(false);
    m_rollback->setAccessibleName(tr("Roll back the last update"));
    row->addWidget(m_rollback, 0);
    m_dismiss = new QToolButton(this);
    m_dismiss->setObjectName(QStringLiteral("zeroModSnapshotDismiss"));
    Style::setProperty(m_dismiss, "zeroVariant", "ghost");
    m_dismiss->setIcon(Icons::icon(QStringLiteral("x"), Theme::current().textSecondary));
    m_dismiss->setIconSize(QSize(14, 14));
    m_dismiss->setAutoRaise(true);
    m_dismiss->setCursor(Qt::PointingHandCursor);
    m_dismiss->setToolTip(tr("Dismiss"));
    m_dismiss->setAccessibleName(tr("Dismiss"));
    row->addWidget(m_dismiss, 0);
    connect(m_rollback, &QPushButton::clicked, this, &ModSnapshotNotice::rollbackRequested);
    connect(m_dismiss, &QToolButton::clicked, this, &ModSnapshotNotice::dismissRequested);
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        m_dismiss->setIcon(Icons::icon(QStringLiteral("x"), Theme::current().textSecondary));
        update();
    });
    hide();
}

void ModSnapshotNotice::setContent(const QString& text, const QString& tooltip)
{
    m_label->setText(text);
    setToolTip(tooltip);
    setAccessibleName(text);
}

void ModSnapshotNotice::setRollbackEnabled(bool enabled, const QString& reason)
{
    m_rollback->setEnabled(enabled);
    m_rollback->setToolTip(enabled ? QString() : reason);
}

void ModSnapshotNotice::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Content::Draw::withAlpha(t.info, Tokens::statusTint * 3), 1));
    p.setBrush(Content::Draw::withAlpha(t.info, Tokens::statusTint));
    p.drawPath(Paint::panelPath(r, t.radiusMd));
    Icons::icon(QStringLiteral("clock"), t.info).paint(&p, QRect(Tokens::spaceMd, (height() - kIconSize) / 2, kIconSize, kIconSize));
}

}  // namespace ModsUi
}  // namespace Zero
