// SPDX-License-Identifier: GPL-3.0-only
#include "InstallDialogParts.h"

#include <QHBoxLayout>
#include <QLinearGradient>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/ContentDraw.h"
#include "ui/zero/content/RemoteImageCache.h"

namespace Zero::Content {

// ---------------------------------------------------------------------------------------------------------------------
// PackIconWidget
// ---------------------------------------------------------------------------------------------------------------------

PackIconWidget::PackIconWidget(const QString& url, RemoteImageCache* images, QWidget* parent, const QString& glyph, int size)
    : QWidget(parent), m_url(url), m_images(images), m_glyph(glyph), m_size(size)
{
    setFixedSize(size, size);
    if (m_images) {
        connect(m_images, &RemoteImageCache::imageReady, this, [this](const QString& ready, int) {
            if (ready == m_url) {
                update();
            }
        });
        connect(m_images, &RemoteImageCache::imageFailed, this, [this](const QString& failed) {
            if (failed == m_url) {
                update();
            }
        });
    }
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

bool PackIconWidget::settled() const
{
    if (m_url.isEmpty() || !m_images || m_images->hasFailed(m_url)) {
        return true;
    }
    return !m_images->peek(m_url, RemoteImageCache::bucketFor(m_size, devicePixelRatioF())).isNull();
}

void PackIconWidget::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    const qreal radius = m_size >= kHeaderSize ? t.radiusMd : t.radiusSm;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = rect();
    const QPainterPath shape = Paint::panelPath(QRectF(r), radius);
    p.setPen(Qt::NoPen);
    p.setBrush(t.elevated);
    p.drawPath(shape);
    QPixmap pixmap;
    if (!m_url.isEmpty() && m_images && !m_images->hasFailed(m_url)) {
        pixmap = m_images->request(m_url, RemoteImageCache::bucketFor(m_size, devicePixelRatioF()), this);
    }
    if (pixmap.isNull()) {
        p.setPen(QPen(t.border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(Paint::panelPath(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), radius));
        const int glyph = m_size >= kHeaderSize ? 24 : qMax(12, m_size / 2);
        Icons::icon(m_glyph, t.textTertiary).paint(&p, QRect(r.center().x() - glyph / 2, r.center().y() - glyph / 2, glyph, glyph));
        return;
    }
    p.setClipPath(shape);
    Draw::drawCover(&p, r, pixmap);
}

// ---------------------------------------------------------------------------------------------------------------------
// DependencySkeleton
// ---------------------------------------------------------------------------------------------------------------------

DependencySkeleton::DependencySkeleton(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(2 * kRowHeight + Tokens::spaceSm);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void DependencySkeleton::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_clock.restart();
    if (Motion::enabled()) {
        m_timer.start();
    }
}

void DependencySkeleton::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_timer.stop();
}

void DependencySkeleton::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QBrush brush(t.border);
    if (m_timer.isActive()) {
        const qreal phase = qreal(m_clock.elapsed() % 1400) / 1400;
        const qreal span = width() * 0.35;
        const qreal x = -span + phase * (width() + 2 * span);
        QLinearGradient g(QPointF(x - span, 0), QPointF(x + span, 0));
        g.setColorAt(0.0, t.border);
        g.setColorAt(0.5, t.borderStrong.lighter(118));
        g.setColorAt(1.0, t.border);
        g.setSpread(QGradient::PadSpread);
        brush = QBrush(g);
    }
    static const qreal names[] = { 0.30, 0.22 };
    static const qreal versions[] = { 0.12, 0.16 };
    for (int row = 0; row < 2; ++row) {
        const int y = row * (kRowHeight + Tokens::spaceSm);
        p.fillPath(Paint::panelPath(QRectF(0, y + (kRowHeight - kRowIcon) / 2, kRowIcon, kRowIcon), t.radiusSm), brush);
        const qreal left = kRowIcon + Tokens::spaceMd;
        p.fillPath(Paint::panelPath(QRectF(left, y + 5, width() * names[row], 10), t.radiusSm), brush);
        p.fillPath(Paint::panelPath(QRectF(left, y + 20, width() * versions[row], 8), t.radiusSm), brush);
    }
}

namespace Parts {

// ---------------------------------------------------------------------------------------------------------------------
// FooterBar and small helpers
// ---------------------------------------------------------------------------------------------------------------------

FooterBar::FooterBar(QWidget* parent) : QWidget(parent)
{
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void FooterBar::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.fillRect(rect(), t.surface);
    p.fillRect(QRect(0, 0, width(), 1), t.border);
}

QLabel* overline(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, Role::Overline);
    return label;
}

void clearLayout(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();  // deleteLater() waits for the event loop: never leave the old pill painted at its old spot
            widget->setParent(nullptr);  // ... nor findChildren() of the container return it until then
            widget->deleteLater();
        }
        delete item;
    }
}

PillTag::Tone versionTypeTone(ModpackVersion::Type type)
{
    switch (type) {
        case ModpackVersion::Type::Release:
            return PillTag::Tone::Success;
        case ModpackVersion::Type::Beta:
            return PillTag::Tone::Warning;
        case ModpackVersion::Type::Alpha:
            return PillTag::Tone::Danger;
        case ModpackVersion::Type::Unknown:
            break;
    }
    return PillTag::Tone::Plain;
}

// ---------------------------------------------------------------------------------------------------------------------
// Header, note, footer
// ---------------------------------------------------------------------------------------------------------------------

HeaderParts buildHeader(QWidget* parent, const ModpackItem& item, RemoteImageCache* images, const QString& nameAccessible, const QString& glyph)
{
    HeaderParts parts;
    parts.root = new QWidget(parent);
    auto* headerRow = new QHBoxLayout(parts.root);
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(Tokens::spaceLg);
    parts.icon = new PackIconWidget(item.iconUrl, images, parts.root, glyph);
    headerRow->addWidget(parts.icon, 0, Qt::AlignTop);
    auto* titles = new QVBoxLayout;
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(2);
    parts.name = new QLabel(item.name, parts.root);
    Style::applyRole(parts.name, Role::Title);
    parts.name->setWordWrap(true);
    parts.name->setAccessibleName(nameAccessible);
    titles->addWidget(parts.name);
    parts.author = new QLabel(item.author.isEmpty() ? QString() : QObject::tr("by %1").arg(item.author), parts.root);
    Style::applyRole(parts.author, Role::Caption);
    Style::setProperty(parts.author, "zeroTone", "secondary");
    parts.author->setVisible(!item.author.isEmpty());
    titles->addWidget(parts.author);
    parts.pills = new QWidget(parts.root);
    auto* pills = new QHBoxLayout(parts.pills);
    pills->setContentsMargins(0, Tokens::spaceXs, 0, 0);
    pills->setSpacing(6);
    pills->addWidget(new PillTag(providerBadge(item.provider), PillTag::Tone::Plain, parts.pills));
    pills->addStretch(1);
    titles->addWidget(parts.pills);
    titles->addStretch(1);
    headerRow->addLayout(titles, 1);
    return parts;
}

NoteParts buildNoteRow(QWidget* parent)
{
    NoteParts parts;
    parts.row = new QWidget(parent);
    auto* noteLayout = new QHBoxLayout(parts.row);
    noteLayout->setContentsMargins(0, 0, 0, 0);
    noteLayout->setSpacing(Tokens::spaceSm);
    parts.icon = new QLabel(parts.row);
    parts.icon->setFixedSize(16, 16);
    noteLayout->addWidget(parts.icon, 0, Qt::AlignTop);
    parts.text = new QLabel(parts.row);
    Style::applyRole(parts.text, Role::Caption);
    Style::setProperty(parts.text, "zeroTone", "info");
    parts.text->setWordWrap(true);
    noteLayout->addWidget(parts.text, 1);
    return parts;
}

FooterParts buildFooter(QWidget* parent, const QString& primaryText)
{
    FooterParts parts;
    parts.bar = new FooterBar(parent);
    auto* footer = new QHBoxLayout(parts.bar);
    footer->setContentsMargins(Tokens::spaceXl, Tokens::spaceMd + 1, Tokens::spaceXl, Tokens::spaceMd);
    footer->setSpacing(Tokens::spaceLg);
    footer->addStretch(1);
    parts.cancel = new QPushButton(QObject::tr("Cancel"), parts.bar);
    Style::setProperty(parts.cancel, "zeroVariant", "ghost");
    parts.cancel->setAutoDefault(false);
    footer->addWidget(parts.cancel);
    parts.primary = new QPushButton(primaryText, parts.bar);
    Style::setProperty(parts.primary, "zeroVariant", "primary");
    Style::setProperty(parts.primary, "zeroSize", "lg");
    parts.primary->setDefault(true);
    parts.primary->setAutoDefault(true);
    parts.primary->setEnabled(false);
    footer->addWidget(parts.primary);
    return parts;
}

}  // namespace Parts
}  // namespace Zero::Content
