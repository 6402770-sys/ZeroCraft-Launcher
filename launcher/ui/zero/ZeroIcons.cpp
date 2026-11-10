// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroIcons.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QSvgRenderer>
#include <QtDebug>

static void zeroInitResources()
{
    Q_INIT_RESOURCE(zero);
}

namespace Zero {

void initResources()
{
    static bool done = false;
    if (!done) {
        zeroInitResources();
        done = true;
    }
}

namespace Icons {

namespace {

QString colorToSvg(const QColor& c)
{
    return c.name(QColor::HexRgb);
}

class TintedSvgEngine : public QIconEngine {
   public:
    TintedSvgEngine(const QByteArray& svg, qreal opacity) : m_svg(svg), m_opacity(opacity) { m_renderer.load(m_svg); }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        if (!m_renderer.isValid())
            return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(painter->opacity() * m_opacity * (mode == QIcon::Disabled ? 0.4 : 1.0));
        m_renderer.render(painter, QRectF(rect));
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override { return scaledPixmap(size, mode, state, 1.0); }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        if (size.isEmpty())
            return {};
        QPixmap pm(size * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }

    QIconEngine* clone() const override { return new TintedSvgEngine(m_svg, m_opacity); }

    QString key() const override { return QStringLiteral("zero-lucide"); }

   private:
    QByteArray m_svg;
    qreal m_opacity;
    mutable QSvgRenderer m_renderer;
};

QByteArray rawSvg(const QString& name)
{
    initResources();
    QFile f(QStringLiteral(":/zero/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

}  // namespace

QByteArray tintedSvg(const QString& name, const QColor& color)
{
    auto svg = rawSvg(name);
    if (svg.isEmpty())
        return {};
    svg.replace("currentColor", colorToSvg(color).toUtf8());
    return svg;
}

QIcon icon(const QString& name, const QColor& color)
{
    static QHash<QPair<QString, QRgb>, QIcon> cache;
    const auto key = qMakePair(name, color.rgba());
    if (auto it = cache.constFind(key); it != cache.constEnd())
        return *it;

    auto svg = tintedSvg(name, color);
    if (svg.isEmpty()) {
        qWarning() << "ZeroIcons: unknown icon" << name;
        return {};
    }
    QIcon ic(new TintedSvgEngine(svg, color.alphaF()));
    cache.insert(key, ic);
    return ic;
}

QPixmap pixmap(const QString& name, const QColor& color, int logicalSize, qreal dpr)
{
    return icon(name, color).pixmap(QSize(logicalSize, logicalSize), dpr);
}

QStringList availableNames()
{
    initResources();
    QStringList out;
    for (const auto& f : QDir(QStringLiteral(":/zero/icons")).entryList({ "*.svg" }))
        out << f.left(f.size() - 4);
    return out;
}

}  // namespace Icons
}  // namespace Zero
