// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroShell.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLayout>
#include <QPainter>
#include <QShortcut>
#include <QShowEvent>
#include <QStackedWidget>
#include <QtMath>
#include <QVBoxLayout>
#include "AccountChip.h"
#include "ShellPage.h"
#include "ZeroVideoBackground.h"
#include "ZeroTopBar.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroNavRail.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTransition.h"
#include "ui/zero/ZeroTokens.h"

namespace Zero {

ZeroShell::ZeroShell(QWidget* parent)
    : QWidget(parent), m_rail(new ZeroNavRail(this)), m_topBar(new ZeroTopBar(this)), m_stack(new QStackedWidget(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_rail);

    auto* column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(m_topBar);
    column->addWidget(m_stack, 1);
    layout->addLayout(column, 1);

    connect(m_rail, &ZeroNavRail::currentChanged, this, &ZeroShell::showPage);
    connect(m_topBar, &ZeroTopBar::accountMenuRequested, this, &ZeroShell::accountMenuRequested);
    connect(m_topBar, &ZeroTopBar::breadcrumbClicked, this, [this]() {
        if (ShellPage* page = m_pages.value(m_current))
            page->breadcrumbActivated();
    });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        // The background file may have been replaced in place (an edited theme keeps its "background.png"): drop the decoded
        // copy when its modification time changed. Checked here, once per theme change, never per paint.
        if (!m_bgPath.isEmpty() && QFileInfo(m_bgPath).lastModified().toMSecsSinceEpoch() != m_bgStamp)
            m_bgPath.clear();
        syncVideo();
        update();
    });
    syncVideo();
}

void ZeroShell::syncVideo()
{
    const auto& t = Theme::current();
    if (t.backgroundVideo.isEmpty()) {
        if (m_video)
            m_video->setSource(QString());  // stops and releases the decoder
        return;
    }
    if (!m_video) {
        m_video = new ZeroVideoBackground(this);
        connect(m_video, &ZeroVideoBackground::frameChanged, this, qOverload<>(&QWidget::update));
    }
    updateVideoConditions();
    m_video->setSound(t.videoSound, t.videoVolume);  // before the source: a new player gets its audio output (or none) right away
    m_video->setSource(t.backgroundVideo);
}

void ZeroShell::updateVideoConditions()
{
    if (!m_video)
        return;
    const QWidget* top = window();
    // A hidden window (e.g. sent to the tray) counts as minimized: nobody sees the video, so it must not decode.
    const bool away = top && (top->isMinimized() || !top->isVisible());
    m_video->setConditions(m_current, away, m_gameRunning, Theme::current().reduceMotion);
}

void ZeroShell::setGameRunning(bool running)
{
    if (m_gameRunning == running)
        return;
    m_gameRunning = running;
    updateVideoConditions();
}

void ZeroShell::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QWidget* top = window();
    if (top != m_watchedWindow) {
        if (m_watchedWindow)
            m_watchedWindow->removeEventFilter(this);
        m_watchedWindow = top;
        if (top && top != this)
            top->installEventFilter(this);
    }
    updateVideoConditions();
}

bool ZeroShell::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_watchedWindow &&
        (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Hide || event->type() == QEvent::Show))
        updateVideoConditions();
    return QWidget::eventFilter(watched, event);
}

const QPixmap& ZeroShell::coverBackground(const QString& path)
{
    if (path != m_bgPath) {
        m_bgPath = path;
        m_bgStamp = QFileInfo(path).lastModified().toMSecsSinceEpoch();
        m_bgSource = QPixmap(path);
        m_bgScaled = QPixmap();
        m_bgScaledSize = QSize();
    }
    const qreal dpr = devicePixelRatioF();
    const QSize target = (QSizeF(size()) * dpr).toSize();
    if (m_bgSource.isNull() || target.isEmpty())
        return m_bgScaled;  // null: nothing to draw
    if (target != m_bgScaledSize || m_bgScaled.isNull()) {
        // Cover: scale until both sides are filled, crop the overflow evenly (centered). Only the visible part is scaled.
        const QSize src = m_bgSource.size();
        const qreal scale = qMax(qreal(target.width()) / src.width(), qreal(target.height()) / src.height());
        const QSize crop(qMin(src.width(), qCeil(target.width() / scale)), qMin(src.height(), qCeil(target.height() / scale)));
        const QRect from(QPoint((src.width() - crop.width()) / 2, (src.height() - crop.height()) / 2), crop);
        m_bgScaled = m_bgSource.copy(from).scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        m_bgScaled.setDevicePixelRatio(dpr);
        m_bgScaledSize = target;
    }
    return m_bgScaled;
}

void ZeroShell::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    // A custom theme's background video: the current frame, cover-cropped + dim, drawn straight from the decoded image. Until the
    // first frame arrives, or when the video cannot be played, the flat background below shows instead.
    if (!t.backgroundVideo.isEmpty() && m_video && !m_video->failed() && !m_video->frame().isNull()) {
        const QImage& frame = m_video->frame();
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF(rect()), frame, QRectF(ZeroVideoBackground::coverRect(frame.size(), size())));
        p.fillRect(rect(), withAlpha(t.bg, qBound(0.0, t.backgroundDim, 1.0)));
        return;
    }
    // A custom theme's background image (cover + dim) sits behind the transparent top bar and pages.
    if (!t.backgroundImage.isEmpty()) {
        const QPixmap& image = coverBackground(t.backgroundImage);
        if (!image.isNull()) {
            QPainter p(this);
            p.drawPixmap(0, 0, image);
            p.fillRect(rect(), withAlpha(t.bg, qBound(0.0, t.backgroundDim, 1.0)));
            return;
        }
    } else if (!m_bgPath.isEmpty()) {
        m_bgPath.clear();  // free the decoded image once the theme no longer uses one
        m_bgSource = m_bgScaled = QPixmap();
        m_bgScaledSize = QSize();
    }
    // Textured themes carry the stone tile behind the (transparent) top bar and pages; flat themes let the window color show.
    if (!t.texture)
        return;
    QPainter p(this);
    Paint::paintBackground(p, rect(), t.bg);
}

void ZeroShell::addPage(const QString& id, const QString& iconName, const QString& label, ShellPage* page, Placement placement)
{
    if (placement == Placement::Bottom && !m_hasStretch) {
        m_rail->addStretch();
        m_hasStretch = true;
    }
    m_pages.insert(id, page);
    m_stack->addWidget(page);
    m_rail->addItem(id, iconName, label);

    // Alt+1..Alt+9 in the order the pages were added, window-wide.
    const int n = m_pages.size();
    if (n <= 9) {
        const QKeySequence keys(Qt::ALT | static_cast<Qt::Key>(Qt::Key_0 + n));
        m_shortcuts.insert(id, keys);
        m_rail->setItemShortcutHint(id, keys.toString(QKeySequence::NativeText));
        auto* shortcut = new QShortcut(keys, this);
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, id]() { setCurrentPage(id); });
    }

    if (m_current.isEmpty())
        setCurrentPage(id);
}

void ZeroShell::finishRail()
{
    if (m_railFinished)
        return;
    m_railFinished = true;

    if (!m_hasStretch) {
        m_rail->addStretch();
        m_hasStretch = true;
    }

    auto* more = m_rail->addActionItem(QStringLiteral("more"), QStringLiteral("ellipsis"), tr("More"));
    connect(more, &QAbstractButton::clicked, this,
            [this, more]() { emit moreMenuRequested(more->mapToGlobal(QPoint(more->width() + Tokens::spaceSm, more->height()))); });

    if (m_settingsPage) {
        m_pages.insert(QStringLiteral("settings"), m_settingsPage);
        m_stack->addWidget(m_settingsPage);
        m_rail->addItem(QStringLiteral("settings"), QStringLiteral("settings"), tr("Settings"));
        m_rail->setItemShortcutHint(QStringLiteral("settings"), QKeySequence(Qt::CTRL | Qt::Key_Comma).toString(QKeySequence::NativeText));
    } else {
        auto* settings = m_rail->addActionItem(QStringLiteral("settings"), QStringLiteral("settings"), tr("Settings"));
        connect(settings, &QAbstractButton::clicked, this, &ZeroShell::settingsRequested);
    }
}

void ZeroShell::setSettingsPage(ShellPage* page)
{
    m_settingsPage = page;
}

void ZeroShell::setCurrentPage(const QString& id)
{
    if (!m_pages.contains(id))
        return;
    m_rail->setCurrent(id);  // emits currentChanged -> showPage (no-op when already current)
    showPage(id);
}

void ZeroShell::showPage(const QString& id)
{
    ShellPage* page = m_pages.value(id);
    if (!page || id == m_current)
        return;
    const bool first = m_current.isEmpty();
    // Page enter: snapshot overlay (old page fades out, new one rises 10 px + fades in). The real page is current and
    // interactive immediately; with motion off no overlay is created.
    const Transition::Snapshot before = first ? Transition::Snapshot() : Transition::capture(m_stack);
    m_current = id;
    m_stack->setCurrentWidget(page);
    updateVideoConditions();
    m_topBar->showPage(page, !first);  // title / subtitle / actions crossfade
    if (!before.isNull()) {
        m_stack->layout()->activate();  // the new page needs its geometry before its picture is taken
        Transition::run(m_stack, before, Transition::Kind::Cascade, Tokens::durCascade);
    }
    emit pageChanged(id);
}

void ZeroShell::setAccount(const QString& name, const QString& caption, const QPixmap& face)
{
    m_topBar->accountChip()->setAccount(name, caption, face);
}

void ZeroShell::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateCollapsed();
}

void ZeroShell::updateCollapsed()
{
    m_rail->setCollapsed(width() < kCollapseBelow);
}

}  // namespace Zero
