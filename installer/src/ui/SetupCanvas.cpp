// SPDX-License-Identifier: GPL-3.0-only
#include "SetupCanvas.h"

#include "Motion.h"
#include "PixelWidgets.h"
#include "SetupTokens.h"
#include "ZeroPixelFont.h"
#include "ZeroScene.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <cmath>

namespace ZeroSetup {

namespace T = Tok;

namespace {

// The scene's six colours: the Minecraft Theme (SetupTokens.h)
Zero::Scene::Palette scenePalette()
{
    return { T::bg, T::surfaceHover, T::accentPressed, T::accent, T::bevelDark, T::text };
}

}  // namespace

SetupCanvas::SetupCanvas(QWidget* parent) : QWidget(parent), m_timer(new QTimer(this))
{
    setFixedSize(T::winW, T::winH);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);

    // deterministic stars (same sky on every run), kept out of the box where the hero and tagline are
    m_stars = Zero::Scene::makeStars(T::winW, T::winH, T::starCount, QRectF(230, 30, 300, 240), T::starSkyFraction, T::starSize);

    m_markStart = Motion::now();
    m_timer->setInterval(T::frameMs);
    connect(m_timer, &QTimer::timeout, this, &SetupCanvas::tick);
    if (Motion::enabled() && !Motion::frozen())
        m_timer->start();
}

void SetupCanvas::setWindowCaption(const QString& s)
{
    m_caption = s;
    update(QRect(0, 0, 300, T::titleH));
}

void SetupCanvas::setTagline(const QString& s)
{
    if (s == m_tagline)
        return;
    m_prevTagline = m_tagline;
    m_tagline = s;
    m_tagChangeMs = Motion::now();
    update(QRect(0, T::taglineTop - 8, T::winW, 40));
}

void SetupCanvas::setHeroShift(double t)
{
    m_shift = t;
    update(QRect(0, 0, T::winW, T::taglineTop + 40));
}

void SetupCanvas::setBobbing(bool on)
{
    m_bob = on;
    update(QRect(0, 0, T::winW, T::taglineTop));
}

void SetupCanvas::setMarkStart(qint64 ms)
{
    m_markStart = ms;
    update();
}

void SetupCanvas::setMarkVisible(bool on)
{
    m_markVisible = on;
    update(QRect(0, 0, T::winW, T::taglineTop + 40));
}

void SetupCanvas::tick()
{
    ++m_tickCount;
    const qint64 now = Motion::now();
    // stars: every other frame (about 30 fps)
    if ((m_tickCount & 1) == 0) {
        QRegion r;
        for (const Zero::Scene::Star& s : std::as_const(m_stars))
            r += QRect(int(s.pos.x()) - 1, int(s.pos.y()) - 1, T::starSize + 2, T::starSize + 2);
        update(r);
    }
    const bool dropping = now - m_markStart < Zero::Scene::markBuildMs(markBlockCount()) + 50;
    const bool fading = now - m_tagChangeMs < T::fadeMs + 50;
    if (dropping || m_bob || fading || m_shift > 0)
        update(QRect(100, 0, 560, T::taglineTop + 40));
}

void SetupCanvas::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && e->position().y() < T::titleH)
        emit dragRequested();
    QWidget::mousePressEvent(e);
}

void SetupCanvas::rebuildBackground()
{
    const qreal dpr = devicePixelRatioF();
    m_bgDpr = dpr;
    m_bg = QPixmap(QSize(int(std::ceil(T::winW * dpr)), int(std::ceil(T::winH * dpr))));
    m_bg.setDevicePixelRatio(dpr);
    QPainter p(&m_bg);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(QRect(0, 0, T::winW, T::winH), T::c(T::bg));

    // stone strip with its grass edge (DESIGN.md "Texture"): the shared scene
    Zero::Scene::paintGround(p, T::winW, T::winH - T::groundH, T::groundH, T::grassH, T::stoneBlock, scenePalette());
}

void SetupCanvas::paintWordmark(QPainter& p, double alpha)
{
    Zero::Scene::paintWordmark(p, QRectF(0, T::wordmarkTop, T::winW, 60), Zero::Scene::snapPixelSize(T::pxWordmark, devicePixelRatioF()),
                               T::wordmarkSpacing, 2, alpha, scenePalette());
}

void SetupCanvas::paintMark(QPainter& p, double baseOpacity)
{
    const double originX = (T::winW - 16 * T::markCell) / 2.0;
    const bool motion = Motion::enabled();
    const double bob = Zero::Scene::bobOffset(Motion::now(), m_bob && motion, T::bobPx);
    Zero::Scene::paintMark(p, QPointF(originX, T::markTop), T::markCell, Motion::now() - m_markStart, motion, bob, T::dropFromPx, baseOpacity);
}

void SetupCanvas::paintHero(QPainter& p)
{
    p.save();
    const double visible = 1.0 - m_shift;
    p.translate(-m_shift * T::heroSlidePx, 0);
    if (m_markVisible && visible > 0.0) {
        paintMark(p, visible);
        paintWordmark(p, visible);
    }
    // tagline with a cross-fade from the previous text
    p.setFont(uiFont(T::pxTagline));
    const QRectF box(0, T::taglineTop, T::winW, 30);
    const double fade = Motion::progress(m_tagChangeMs, T::fadeMs, QEasingCurve::OutCubic);
    if (fade < 1.0 && !m_prevTagline.isEmpty()) {
        p.setOpacity(visible * (1.0 - fade));
        p.setPen(T::c(T::textSecondary));
        p.drawText(box, Qt::AlignHCenter | Qt::AlignTop, m_prevTagline);
    }
    p.setOpacity(visible * fade);
    p.setPen(T::c(T::textSecondary));
    p.drawText(box, Qt::AlignHCenter | Qt::AlignTop, m_tagline);
    p.restore();
}

void SetupCanvas::paintEvent(QPaintEvent* e)
{
    if (m_bg.isNull() || !qFuzzyCompare(m_bgDpr, devicePixelRatioF()))
        rebuildBackground();
    QPainter p(this);
    p.setClipRegion(e->region());
    p.drawPixmap(0, 0, m_bg);

    // stars
    Zero::Scene::paintStars(p, m_stars, T::starSize, Motion::now(), Motion::enabled(), T::text);

    // title strip caption
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(uiFont(T::pxTitleStrip, QFont::DemiBold));
    p.setPen(T::c(T::textTertiary));
    p.drawText(QRectF(16, 0, 300, T::titleH), Qt::AlignVCenter | Qt::AlignLeft, m_caption);

    paintHero(p);
}

}  // namespace ZeroSetup
