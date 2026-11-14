// SPDX-License-Identifier: GPL-3.0-only
/*
 *  ZERO Launcher - first-run intro. See IntroOverlay.h.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "IntroOverlay.h"

#include <QAbstractButton>
#include <QApplication>
#include <QChildEvent>
#include <QEasingCurve>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QList>
#include <QPainter>
#include <QPen>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <algorithm>
#include <cmath>

#include "settings/SettingsObject.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTokens.h"

namespace Zero {
namespace Onboarding {

namespace {

namespace M = Tokens::Minecraft;

constexpr int kFrameMs = 33;  // about 30 fps while the scene idles
constexpr int kSkipMargin = 16;
constexpr int kMinButtonWidth = 200;
constexpr int kDesignWidth = 760, kDesignHeight = 460;  // the installer window the scene was drawn for

// The intro is the same night scene in every theme (a brand moment): the Minecraft palette of ZeroTokens.h.
Scene::Palette scenePalette()
{
    return { M::bg, M::surfaceHover, M::accentPressed, M::accent, M::bevelDark, M::text };
}

QColor withAlpha(QRgb v, double a)
{
    QColor c = QColor::fromRgba(v);
    c.setAlphaF(std::clamp(a, 0.0, 1.0));
    return c;
}

/// "Skip intro" (tertiary text button) + the small "Esc" key hint, always in the top-right corner. Painted with the scene's colours:
/// it sits on the night sky, whatever the theme is.
class SkipButton : public QAbstractButton {
   public:
    explicit SkipButton(QWidget* parent) : QAbstractButton(parent)
    {
        setText(QObject::tr("Skip intro"));
        setAccessibleName(QObject::tr("Skip intro"));
        setToolTip(QObject::tr("Skip intro (Esc)"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(Style::font(Role::Label));
        const QFontMetrics hint(Style::font(Role::Caption));
        return QSize(kPadX + fm.horizontalAdvance(text()) + kGap + keyWidth(hint) + kPadX, 30);
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        const bool hot = underMouse() || isDown();
        const QFont label = Style::font(Role::Label);
        const QFont caption = Style::font(Role::Caption);
        const QFontMetrics fm(label), hint(caption);

        if (hasFocus()) {  // the diamond focus ring
            p.setPen(QPen(QColor::fromRgba(M::accent2), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
        }
        p.setFont(label);
        p.setPen(QColor::fromRgba(hot ? M::text : M::textSecondary));
        const QRect textRect(kPadX, 0, fm.horizontalAdvance(text()), height());
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text());

        // the key hint: a small outlined pill
        p.setFont(caption);
        const int kw = keyWidth(hint);
        const QRectF pill(kPadX + textRect.width() + kGap, (height() - hint.height() - 4) / 2.0, kw, hint.height() + 4);
        p.setPen(QPen(QColor::fromRgba(M::borderStrong), 1));
        p.setBrush(withAlpha(M::surface, 0.6));
        p.drawRect(pill.adjusted(0.5, 0.5, -0.5, -0.5));
        p.setPen(QColor::fromRgba(M::textTertiary));
        p.drawText(pill, Qt::AlignCenter, QObject::tr("Esc"));
    }

   private:
    static int keyWidth(const QFontMetrics& hint) { return hint.horizontalAdvance(QObject::tr("Esc")) + 10; }
    static constexpr int kPadX = 8;
    static constexpr int kGap = 8;
};

double outCubic(double t)
{
    const double c = std::clamp(t, 0.0, 1.0);
    return 1.0 - std::pow(1.0 - c, 3.0);
}

}  // namespace

IntroOverlay::IntroOverlay(QWidget* host, SettingsObject* settings)
    : QWidget(host), m_host(host), m_settings(settings), m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("zeroIntroOverlay"));
    setAccessibleName(tr("Welcome to ZERO Launcher"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    m_start = new QPushButton(tr("Get started"), this);
    m_start->setAccessibleName(tr("Get started"));
    Style::setProperty(m_start, "zeroVariant", "primary");
    Style::setProperty(m_start, "zeroSize", "lg");
    m_start->setLayoutDirection(Qt::RightToLeft);  // the arrow after the text
    m_start->setFocusPolicy(Qt::StrongFocus);
    m_start->hide();
    connect(m_start, &QPushButton::clicked, this, &IntroOverlay::getStarted);

    m_skip = new SkipButton(this);
    m_skip->adjustSize();
    connect(m_skip, &QAbstractButton::clicked, this, &IntroOverlay::skip);

    refreshIcon();
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcon();
        update();
    });

    if (host) {
        setGeometry(host->rect());
        host->installEventFilter(this);
    }
    qApp->installEventFilter(this);  // keeps the shell's shortcuts quiet while the intro is up

    m_clock.start();
    m_timer->setInterval(kFrameMs);
    connect(m_timer, &QTimer::timeout, this, &IntroOverlay::tick);

    if (Motion::enabled()) {
        // the two buttons fade in on their own; with reduced motion they have no effect (the whole overlay fades instead, below)
        m_startFx = new QGraphicsOpacityEffect(m_start);
        m_start->setGraphicsEffect(m_startFx);
        m_skipFx = new QGraphicsOpacityEffect(m_skip);
        m_skip->setGraphicsEffect(m_skipFx);
    }
    relayout();
    syncChildren();
    if (Motion::enabled()) {
        m_timer->start();
    } else {
        // Reduced motion: no drops, slides, bobbing or twinkle. The whole scene comes in with one short fade.
        m_rootFx = new QGraphicsOpacityEffect(this);
        m_rootFx->setOpacity(0.0);
        setGraphicsEffect(m_rootFx);
        auto* fade = new QPropertyAnimation(m_rootFx, "opacity", this);
        fade->setDuration(kReducedFadeMs);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        connect(fade, &QPropertyAnimation::finished, this, [this, fade]() {
            fade->deleteLater();
            if (m_rootFx)
                setGraphicsEffect(nullptr);  // deletes the effect
        });
        fade->start();
    }

    show();
    raise();
    setFocus(Qt::OtherFocusReason);
}

IntroOverlay::~IntroOverlay()
{
    if (qApp)
        qApp->removeEventFilter(this);
}

qint64 IntroOverlay::timelineMs() const
{
    if (m_frozen >= 0)
        return m_frozen;
    if (!Motion::enabled())
        return kFinalMs;
    return m_clock.elapsed() + m_offset;
}

bool IntroOverlay::startButtonShown() const
{
    return !m_start->isHidden();
}

void IntroOverlay::freezeAt(qint64 ms)
{
    m_frozen = std::max<qint64>(0, ms);
    m_timer->stop();
    syncChildren();
    update();
}

void IntroOverlay::jumpToEnd()
{
    if (m_frozen >= 0) {
        m_frozen = kFinalMs;
    } else {
        const qint64 t = timelineMs();
        if (t < kFinalMs)
            m_offset += kFinalMs - t;
    }
    syncChildren();
    update();
}

void IntroOverlay::skip()
{
    if (m_finishing)
        return;
    if (timelineMs() < kButtonStartMs)
        jumpToEnd();
    else
        getStarted();
}

void IntroOverlay::getStarted()
{
    if (m_finishing)
        return;
    m_finishing = true;
    m_timer->stop();
    if (m_settings)
        m_settings->set(QStringLiteral("ZeroIntroSeen"), true);
    qApp->removeEventFilter(this);  // the shell's shortcuts work again while the overlay fades away
    if (m_host)
        m_host->removeEventFilter(this);

    auto done = [this]() {
        hide();
        deleteLater();
        emit finished();
    };
    const int ms = Motion::duration(kFadeOutMs);
    if (ms <= 0) {
        done();
        return;
    }
    // Fade out: the overlay's last frame (buttons included) is painted with falling opacity while the real children are hidden.
    m_snapshot = grab();
    m_start->hide();
    m_skip->hide();
    setFocus(Qt::OtherFocusReason);
    auto* fade = new QVariantAnimation(this);
    fade->setDuration(ms);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    connect(fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_fadeOut = v.toDouble();
        update();
    });
    connect(fade, &QVariantAnimation::finished, this, done);
    fade->start();
}

void IntroOverlay::tick()
{
    syncChildren();
    // The scene idles: stars twinkle and the finished mark bobs. Until the last frame everything may change.
    const qint64 t = timelineMs();
    if (t < kFinalMs + 100) {
        update();
        return;
    }
    QRegion r;
    for (const Scene::Star& s : std::as_const(m_stars))
        r += QRect(int(s.pos.x()) - 1, int(s.pos.y()) - 1, m_l.starSize + 2, m_l.starSize + 2);
    const int cell = m_l.cell;
    r += QRect((width() - 16 * cell) / 2 - 2, m_l.markTop - int(std::ceil(Scene::kDesignBobPx * m_l.k)) - 2, 16 * cell + 4,
               16 * cell + int(std::ceil(Scene::kDesignBobPx * m_l.k)) * 2 + 4);
    update(r);
}

void IntroOverlay::syncChildren()
{
    const bool motion = Motion::enabled() || m_frozen >= 0;
    const qint64 t = timelineMs();
    if (m_skipFx)
        m_skipFx->setOpacity(motion ? outCubic(double(t) / kSkyEndMs) : 1.0);
    const bool buttonOn = !motion || t >= kButtonStartMs;
    if (m_startFx)
        m_startFx->setOpacity(motion ? outCubic(double(t - kButtonStartMs) / double(kButtonEndMs - kButtonStartMs)) : 1.0);
    if (buttonOn && m_start->isHidden() && !m_finishing) {
        placeChildren();
        m_start->show();
        m_start->setFocus(Qt::TabFocusReason);
    }
}

void IntroOverlay::refreshIcon()
{
    m_start->setIcon(Icons::icon(QStringLiteral("arrow-right"), Theme::current().onAccent));
    m_start->setIconSize(QSize(18, 18));
}

void IntroOverlay::buildStars()
{
    const int w = width(), h = height();
    const double area = double(w) * h / (double(kDesignWidth) * kDesignHeight);
    const int count = std::clamp(int(std::lround(30 * area)), 24, 80);
    // keep the stars off the hero (mark, wordmark, caption, button)
    const QRectF keepOut(w / 2.0 - 170 * m_l.k, m_l.markTop - 30, 340 * m_l.k, m_l.buttonTop + 80 - m_l.markTop);
    m_stars = Scene::makeStars(w, h, count, keepOut, 0.55, m_l.starSize);
    const QRect skipArea = m_skip->geometry().adjusted(-8, -8, 8, 8);
    m_stars.erase(std::remove_if(m_stars.begin(), m_stars.end(),
                                 [&](const Scene::Star& s) { return skipArea.intersects(QRect(s.pos.toPoint(), QSize(m_l.starSize, m_l.starSize))); }),
                  m_stars.end());
}

void IntroOverlay::relayout()
{
    const int w = std::max(1, width()), h = std::max(1, height());
    Layout l;
    // The installer scene is drawn for 760 x 460 with 6 px cells; the mark cell stays a whole number of pixels so the blocks stay crisp.
    l.cell = std::clamp(int(std::floor(Scene::kDesignCell * std::min(w / double(kDesignWidth), h / double(kDesignHeight)))), 3, 14);
    l.k = l.cell / double(Scene::kDesignCell);
    l.block = std::max(2, int(std::lround(4 * l.k)));
    l.grassH = l.cell;
    l.groundH = int(std::lround(46 * l.k));
    l.groundTop = h - l.groundH;
    l.starSize = std::max(2, int(std::lround(3 * l.k)));

    // Vertical layout: the installer's positions relative to the mark top, scaled; caption and button are real text / widgets.
    m_start->ensurePolished();
    const int buttonH = m_start->sizeHint().height();
    const int captionH = QFontMetrics(Style::font(Role::Body)).height() + 4;
    const int wordOff = int(std::lround(106 * l.k));
    const int captionOff = int(std::lround(172 * l.k));
    const int buttonOff = captionOff + captionH + int(std::lround(30 * l.k));
    const int contentH = buttonOff + buttonH;
    const int minTop = kSkipMargin + 30 + 10;  // below the Skip intro row
    int top = std::max(minTop, (l.groundTop - contentH) / 2);
    top = std::min(top, h - contentH - 8);  // a very small window: never clip the button
    top = std::max(top, 8);
    l.markTop = top;
    l.wordTop = top + wordOff;
    l.captionTop = top + captionOff;
    l.buttonTop = top + buttonOff;
    m_l = l;

    m_ground = QPixmap();
    placeChildren();
    buildStars();
    update();
}

void IntroOverlay::placeChildren()
{
    m_skip->adjustSize();
    m_skip->move(width() - m_skip->width() - kSkipMargin, kSkipMargin);
    m_start->ensurePolished();
    const QSize sh = m_start->sizeHint();
    const int bw = std::max(kMinButtonWidth, sh.width() + 24);
    m_start->setGeometry((width() - bw) / 2, m_l.buttonTop, bw, sh.height());
}

QPixmap IntroOverlay::groundPixmap()
{
    const qreal dpr = devicePixelRatioF();
    const QSize logical(width(), m_l.groundH);
    if (m_ground.isNull() || !qFuzzyCompare(m_groundDpr, dpr) || m_groundSize != logical) {
        m_groundDpr = dpr;
        m_groundSize = logical;
        m_ground = QPixmap(QSize(int(std::ceil(logical.width() * dpr)), int(std::ceil(logical.height() * dpr))));
        m_ground.setDevicePixelRatio(dpr);
        m_ground.fill(Qt::transparent);
        QPainter p(&m_ground);
        Scene::paintGround(p, logical.width(), 0, logical.height(), m_l.grassH, m_l.block, scenePalette());
    }
    return m_ground;
}

void IntroOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (m_finishing && !m_snapshot.isNull()) {
        p.setOpacity(m_fadeOut);
        p.drawPixmap(0, 0, m_snapshot);
        return;
    }
    const bool motion = Motion::enabled() || m_frozen >= 0;
    const qint64 t = timelineMs();
    const Scene::Palette pal = scenePalette();
    const double skyA = motion ? outCubic(double(t) / kSkyEndMs) : 1.0;

    // sky and stars
    p.fillRect(rect(), withAlpha(pal.sky, skyA));
    Scene::paintStars(p, m_stars, m_l.starSize, t, motion, pal.text, skyA);

    // ground rises from the bottom (clipped to the window)
    {
        const double g = motion ? outCubic(double(t - kGroundStartMs) / double(kGroundEndMs - kGroundStartMs)) : 1.0;
        if (g > 0.0) {
            const int y = m_l.groundTop + int(std::lround(m_l.groundH * (1.0 - g)));
            p.drawPixmap(0, y, groundPixmap());
        }
    }

    // the mark builds block by block, then bobs gently
    const double k = m_l.k;
    const bool built = t >= Scene::markBuildMs(int(Scene::markBlocks().size()));
    const double bob = Scene::bobOffset(t, motion && built, Scene::kDesignBobPx * k);
    Scene::paintMark(p, QPointF((width() - 16 * m_l.cell) / 2.0, m_l.markTop), m_l.cell, t, motion, bob, Scene::kDesignDropFromPx * k);

    // wordmark: fades in and rises 12 px
    const double wa = motion ? outCubic(double(t - kWordStartMs) / double(kWordEndMs - kWordStartMs)) : 1.0;
    if (wa > 0.0) {
        const double rise = motion ? kWordRisePx * (1.0 - wa) : 0.0;
        Scene::paintWordmark(p, QRectF(0, m_l.wordTop + rise, width(), 60 * k),
                             Scene::snapPixelSize(int(std::lround(Scene::kDesignWordmarkPx * k)), devicePixelRatioF()),
                             int(std::lround(Scene::kDesignWordmarkSpacing * k)), std::max(2, int(std::lround(2 * k))), wa, pal);
    }

    // caption
    const double ca = motion ? outCubic(double(t - kCaptionStartMs) / double(kCaptionEndMs - kCaptionStartMs)) : 1.0;
    if (ca > 0.0) {
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setFont(Style::font(Role::Body));
        p.setPen(withAlpha(M::textSecondary, ca));
        p.drawText(QRect(0, m_l.captionTop, width(), QFontMetrics(p.font()).height() + 4), Qt::AlignHCenter | Qt::AlignTop, tr("Welcome to ZERO Launcher"));
    }
}

void IntroOverlay::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    relayout();
}

void IntroOverlay::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    raise();
}

void IntroOverlay::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
        case Qt::Key_Escape:
            skip();
            e->accept();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            if (auto* button = qobject_cast<QAbstractButton*>(focusWidget()); button && button != m_start) {
                button->click();  // Enter on the focused Skip intro button
            } else if (!m_start->isHidden()) {
                getStarted();  // Get started
            } else {
                skip();  // before the button appeared: to the final frame
            }
            e->accept();
            return;
        default:
            break;
    }
    e->accept();  // nothing reaches the shell
}

void IntroOverlay::mousePressEvent(QMouseEvent* e)
{
    // a click anywhere before the button appeared jumps to the final frame
    if (e->button() == Qt::LeftButton && !m_finishing && timelineMs() < kButtonStartMs)
        jumpToEnd();
    e->accept();
}

void IntroOverlay::mouseReleaseEvent(QMouseEvent* e)
{
    e->accept();
}

void IntroOverlay::wheelEvent(QWheelEvent* e)
{
    e->accept();
}

bool IntroOverlay::focusNextPrevChild(bool next)
{
    // Tab cycles Get started <-> Skip intro; it never reaches the shell behind.
    QList<QWidget*> chain;
    if (!m_start->isHidden())
        chain << m_start;
    chain << m_skip;
    const int at = int(chain.indexOf(focusWidget()));
    const int n = int(chain.size());
    const int to = at < 0 ? (next ? 0 : n - 1) : (at + (next ? 1 : n - 1)) % n;
    chain[to]->setFocus(next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    return true;
}

bool IntroOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_host.data()) {
        switch (event->type()) {
            case QEvent::Resize:
                setGeometry(m_host->rect());
                break;
            case QEvent::ChildAdded:
            case QEvent::ChildRemoved:
                // anything the window gains later (toasts, palette) must not end up above the intro
                QTimer::singleShot(0, this, [this]() {
                    if (!m_finishing)
                        raise();
                });
                break;
            default:
                break;
        }
        return false;
    }
    if (event->type() == QEvent::ShortcutOverride && !m_finishing) {
        // The shell's shortcuts (Ctrl+K, Alt+N ...) must not fire over the intro: the key goes to the focused widget as a plain key press.
        if (auto* w = qobject_cast<QWidget*>(watched); w && (w == this || isAncestorOf(w))) {
            event->accept();
            return true;  // consumed: the shortcut map sees an accepted override and sends the plain key press instead
        }
    }
    return false;
}

}  // namespace Onboarding
}  // namespace Zero
