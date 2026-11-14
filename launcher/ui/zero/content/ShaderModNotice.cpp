// SPDX-License-Identifier: GPL-3.0-only
#include "ShaderModNotice.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/content/ContentDraw.h"

namespace Zero::Content {

namespace {
constexpr int kIconSize = 16;
}

ShaderModNotice::ShaderModNotice(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("zeroShaderModNotice"));
    setMinimumHeight(36);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(Tokens::spaceMd + kIconSize + Tokens::spaceSm, Tokens::spaceXs, Tokens::spaceSm, Tokens::spaceXs);
    row->setSpacing(Tokens::spaceSm);
    m_label = new QLabel(this);
    Style::applyRole(m_label, Role::Label);
    m_label->setWordWrap(true);
    row->addWidget(m_label, 1);
    m_button = new QPushButton(this);
    Style::setProperty(m_button, "zeroVariant", "ghost");
    m_button->setAutoDefault(false);
    row->addWidget(m_button, 0);
    connect(m_button, &QPushButton::clicked, this, [this]() { emit findRequested(m_notice.searchQuery); });
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    hide();
}

void ShaderModNotice::setNotice(const ShaderNotice& notice)
{
    m_notice = notice;
    m_label->setText(notice.text);
    setAccessibleName(notice.text);
    m_button->setText(notice.buttonText);
    m_button->setAccessibleName(notice.buttonText);
    m_button->setVisible(!notice.buttonText.isEmpty());
    setVisible(notice.show);
    update();
}

void ShaderModNotice::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Draw::withAlpha(t.info, Tokens::statusTint * 3), 1));
    p.setBrush(Draw::withAlpha(t.info, Tokens::statusTint));
    p.drawPath(Paint::panelPath(r, t.radiusMd));
    Icons::icon(QStringLiteral("info"), t.info).paint(&p, QRect(Tokens::spaceMd, (height() - kIconSize) / 2, kIconSize, kIconSize));
}

}  // namespace Zero::Content
