// SPDX-License-Identifier: GPL-3.0-only
#include "ScreenshotViewer.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>
#include "ElidedLabel.h"
#include "FileSystem.h"
#include "ThumbnailCache.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"

namespace Zero {

namespace {
constexpr int kBackdropAlpha = 252;  // of 255: the page only hints through, so the grid text never competes with the image
constexpr int kEdge = Tokens::spaceXl;
constexpr int kTopSpace = Tokens::controlHeight + 2 * Tokens::spaceLg;  // room for the close button
constexpr int kBarHeight = 64;

QString fullDate(qint64 ms)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);
    const QLocale locale;
    return locale.toString(dt.date(), QLocale::LongFormat) + QLatin1Char(' ') + locale.toString(dt.time(), QLocale::ShortFormat);
}

/// The bottom bar: a panel (surface, 1 px border) behind its child widgets.
class ViewerBar : public QWidget {
   public:
    using QWidget::QWidget;

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(t.border, 1));
        p.setBrush(t.surface);
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
    }
};

QPushButton* ghostButton(const QString& text, QWidget* parent)
{
    auto* b = new QPushButton(text, parent);
    Style::setProperty(b, "zeroVariant", "ghost");
    b->setIconSize(QSize(16, 16));
    return b;
}
}  // namespace

ScreenshotViewer::ScreenshotViewer(ThumbnailCache* cache, QWidget* parent) : QWidget(parent), m_cache(cache)
{
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    hide();

    auto* bar = new ViewerBar(this);
    m_bar = bar;
    m_name = new ElidedLabel(bar);
    Style::applyRole(m_name, Role::Label);
    m_meta = new ElidedLabel(bar);
    Style::applyRole(m_meta, Role::Caption);

    m_copy = ghostButton(tr("Copy"), bar);
    m_copy->setToolTip(tr("Copy the image to the clipboard"));
    m_open_ = ghostButton(tr("Open"), bar);
    m_open_->setToolTip(tr("Open with the default app"));
    m_folder = ghostButton(tr("Show in folder"), bar);
    m_instance = ghostButton(tr("Show instance"), bar);
    m_delete = ghostButton(tr("Delete…"), bar);
    m_delete->setToolTip(tr("Move to the Recycle Bin"));
    m_close = ghostButton(QString(), this);
    m_close->setToolTip(tr("Close (Esc)"));
    m_close->setFixedSize(Tokens::controlHeight, Tokens::controlHeight);

    auto* texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);
    texts->addWidget(m_name);
    texts->addWidget(m_meta);
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(Tokens::spaceLg, Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm);
    row->setSpacing(Tokens::spaceXs);
    row->addLayout(texts, 1);
    row->addSpacing(Tokens::spaceMd);
    for (QPushButton* b : { m_copy, m_open_, m_folder, m_instance, m_delete })
        row->addWidget(b);

    connect(m_copy, &QPushButton::clicked, this, &ScreenshotViewer::copyImage);
    connect(m_open_, &QPushButton::clicked, this, &ScreenshotViewer::openExternally);
    connect(m_folder, &QPushButton::clicked, this, &ScreenshotViewer::showInFolder);
    connect(m_instance, &QPushButton::clicked, this, [this]() {
        if (m_index >= 0 && m_index < m_entries.size())
            emit showInstanceRequested(m_entries.at(m_index).instanceId);
    });
    connect(m_delete, &QPushButton::clicked, this, &ScreenshotViewer::askTrash);
    connect(m_close, &QPushButton::clicked, this, &ScreenshotViewer::closeViewer);

    // arrow keys and Esc keep working while a button has the focus
    for (QPushButton* b : { m_copy, m_open_, m_folder, m_instance, m_delete, m_close })
        b->installEventFilter(this);

    if (m_cache)
        connect(m_cache, &ThumbnailCache::thumbnailReady, this, [this](const QString& path) {
            if (m_open && !m_fullLoaded && m_index >= 0 && m_entries.at(m_index).path == path)
                update();
        });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcons();
        update();
    });
    refreshIcons();
    parent->installEventFilter(this);
}

void ScreenshotViewer::refreshIcons()
{
    const auto& t = Theme::current();
    m_copy->setIcon(Icons::icon(QStringLiteral("copy"), t.textSecondary));
    m_open_->setIcon(Icons::icon(QStringLiteral("image"), t.textSecondary));
    m_folder->setIcon(Icons::icon(QStringLiteral("folder-open"), t.textSecondary));
    m_instance->setIcon(Icons::icon(QStringLiteral("library-big"), t.textSecondary));
    m_delete->setIcon(Icons::icon(QStringLiteral("trash-2"), t.danger));
    m_close->setIcon(Icons::icon(QStringLiteral("x"), t.textSecondary));
}

QString ScreenshotViewer::currentPath() const
{
    return m_open && m_index >= 0 && m_index < m_entries.size() ? m_entries.at(m_index).path : QString();
}

void ScreenshotViewer::setEntries(const QList<Gallery::ScreenshotEntry>& entries)
{
    const QString path = currentPath();
    m_entries = entries;
    if (!m_open)
        return;
    int index = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).path == path) {
            index = i;
            break;
        }
    }
    if (index >= 0) {
        m_index = index;
        updateInfo();
    } else if (m_entries.isEmpty()) {
        closeViewer();
    } else {
        showIndex(qBound(0, m_index, int(m_entries.size()) - 1));  // the shown file vanished: its neighbour takes over
    }
}

void ScreenshotViewer::openPath(const QString& path)
{
    int index = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).path == path) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;
    const bool wasOpen = m_open;
    if (!wasOpen)
        m_previousFocus = QApplication::focusWidget();
    m_open = true;
    setGeometry(parentWidget()->rect());
    layoutBar();
    showIndex(index);
    raise();
    show();
    setFocus(Qt::PopupFocusReason);
    if (!wasOpen)
        Motion::fadeIn(this, Tokens::durBase);
}

void ScreenshotViewer::closeViewer()
{
    if (!m_open)
        return;
    const QString last = currentPath();
    m_open = false;
    ++m_token;  // a pending decode is dropped
    m_image = QImage();
    m_scaled = QPixmap();
    m_fullLoaded = false;
    if (m_previousFocus && m_previousFocus->isVisible() && m_previousFocus->isEnabled())
        m_previousFocus->setFocus(Qt::OtherFocusReason);
    emit closed(last);  // the page puts the focus on the grid item
    Motion::fadeOut(this, Tokens::durFast);
    if (!isVisible())
        return;
    if (!Motion::enabled())
        hide();
}

void ScreenshotViewer::showIndex(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_index = index;
    m_image = QImage();
    m_scaled = QPixmap();
    m_fullLoaded = false;
    updateInfo();
    startFullLoad();
    update();
}

void ScreenshotViewer::step(int delta)
{
    const int next = m_index + delta;
    if (next >= 0 && next < m_entries.size())
        showIndex(next);
}

void ScreenshotViewer::updateInfo()
{
    if (m_index < 0 || m_index >= m_entries.size())
        return;
    const Gallery::ScreenshotEntry& e = m_entries.at(m_index);
    static_cast<ElidedLabel*>(m_name)->setFullText(e.fileName);
    static_cast<ElidedLabel*>(m_meta)->setFullText(
        tr("%1 · %2 · %3 · %4 of %5")
            .arg(e.instanceName, fullDate(e.modifiedMs), Format::bytes(e.sizeBytes))
            .arg(m_index + 1)
            .arg(m_entries.size()));
    m_copy->setEnabled(m_fullLoaded);
}

void ScreenshotViewer::startFullLoad()
{
    const quint64 token = ++m_token;
    const QString path = m_entries.at(m_index).path;
    m_copy->setEnabled(false);
    // The pool thread never reads a QPointer: it posts to the application object and the pointer is checked there, on the GUI thread.
    QPointer<ScreenshotViewer> self(this);
    QThreadPool::globalInstance()->start([self, token, path]() {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(
            app,
            [self, token, image]() {
                if (self)
                    self->onFullLoaded(token, image);
            },
            Qt::QueuedConnection);
    });
}

void ScreenshotViewer::onFullLoaded(quint64 token, const QImage& image)
{
    if (token != m_token || !m_open || image.isNull())
        return;
    m_image = image;
    m_fullLoaded = true;
    m_scaled = QPixmap();
    m_copy->setEnabled(true);
    update();
}

QRect ScreenshotViewer::imageArea() const
{
    const int bottom = m_bar->geometry().top() - Tokens::spaceLg;
    return QRect(kEdge, kTopSpace, width() - 2 * kEdge, qMax(0, bottom - kTopSpace));
}

void ScreenshotViewer::layoutBar()
{
    const int w = width() - 2 * kEdge;
    m_bar->setGeometry(kEdge, height() - kEdge - kBarHeight, qMax(0, w), kBarHeight);
    m_close->move(width() - kEdge - m_close->width(), Tokens::spaceLg);
}

void ScreenshotViewer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutBar();
    m_scaled = QPixmap();
}

void ScreenshotViewer::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QColor backdrop = t.bg;
    backdrop.setAlpha(kBackdropAlpha);
    p.fillRect(rect(), backdrop);
    if (m_index < 0 || m_index >= m_entries.size())
        return;

    const QRect area = imageArea();
    if (area.isEmpty())
        return;
    const qreal dpr = devicePixelRatioF();
    QRectF target;
    if (m_fullLoaded && !m_image.isNull()) {
        QSizeF fit = QSizeF(m_image.size()).scaled(area.size(), Qt::KeepAspectRatio);
        if (fit.width() > m_image.width() / dpr)  // never upscale beyond 1:1
            fit = QSizeF(m_image.size()) / dpr;
        target = QRectF(QPointF(area.center()) - QPointF(fit.width() / 2, fit.height() / 2), fit);
        const QSize px = (fit * dpr).toSize();
        if (m_scaled.isNull() || m_scaledFor != px) {
            m_scaled = QPixmap::fromImage(m_image.scaled(px, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            m_scaled.setDevicePixelRatio(dpr);
            m_scaledFor = px;
        }
        p.save();
        p.setClipPath(Paint::panelPath(target, t.radiusMd));
        p.drawPixmap(target.toRect(), m_scaled);
        p.restore();
        return;
    }
    // not decoded yet: the thumbnail, scaled up to the area
    const Gallery::ScreenshotEntry& e = m_entries.at(m_index);
    const QPixmap thumb = m_cache ? m_cache->thumbnail(e.path, e.modifiedMs) : QPixmap();
    if (!thumb.isNull()) {
        const QSizeF fit = QSizeF(thumb.size()).scaled(area.size(), Qt::KeepAspectRatio);
        target = QRectF(QPointF(area.center()) - QPointF(fit.width() / 2, fit.height() / 2), fit);
        p.save();
        p.setClipPath(Paint::panelPath(target, t.radiusMd));
        p.drawPixmap(target.toRect(), thumb);
        p.restore();
    } else {
        const QIcon icon = Icons::icon(QStringLiteral("image"), t.textTertiary);
        icon.paint(&p, QRect(area.center().x() - 20, area.center().y() - 20, 40, 40));
    }
}

void ScreenshotViewer::mousePressEvent(QMouseEvent* event)
{
    const QRect area = imageArea();
    QRectF image = area;
    if (m_fullLoaded && !m_image.isNull()) {
        const qreal dpr = devicePixelRatioF();
        QSizeF fit = QSizeF(m_image.size()).scaled(area.size(), Qt::KeepAspectRatio);
        if (fit.width() > m_image.width() / dpr)
            fit = QSizeF(m_image.size()) / dpr;
        image = QRectF(QPointF(area.center()) - QPointF(fit.width() / 2, fit.height() / 2), fit);
    }
    if (!image.contains(event->position()))
        closeViewer();  // a click on the backdrop
    event->accept();
}

void ScreenshotViewer::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_Escape:
            closeViewer();
            return;
        case Qt::Key_Left:
            step(-1);
            return;
        case Qt::Key_Right:
            step(1);
            return;
        case Qt::Key_Home:
            showIndex(0);
            return;
        case Qt::Key_End:
            showIndex(int(m_entries.size()) - 1);
            return;
        default:
            break;
    }
    QWidget::keyPressEvent(event);
}

bool ScreenshotViewer::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
        setGeometry(parentWidget()->rect());
        return false;
    }
    if (event->type() == QEvent::KeyPress && m_open && watched != parentWidget()) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right || key->key() == Qt::Key_Escape) {
            keyPressEvent(key);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ScreenshotViewer::copyImage()
{
    if (!m_fullLoaded || m_image.isNull())
        return;
    QApplication::clipboard()->setImage(m_image);
    ZeroToast::show(window(), tr("Copied to clipboard"), ZeroToast::Kind::Success);
}

void ScreenshotViewer::openExternally()
{
    if (m_index < 0 || m_index >= m_entries.size())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_entries.at(m_index).path));
}

void ScreenshotViewer::showInFolder()
{
    if (m_index < 0 || m_index >= m_entries.size())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_entries.at(m_index).path).absolutePath()));
}

void ScreenshotViewer::askTrash()
{
    if (m_index < 0 || m_index >= m_entries.size())
        return;
    const Gallery::ScreenshotEntry entry = m_entries.at(m_index);
    QMessageBox box(QMessageBox::Question, tr("Delete screenshot?"), tr("Move %1 to the Recycle Bin?").arg(entry.fileName),
                    QMessageBox::NoButton, window());
    box.setInformativeText(tr("You can restore it from the Recycle Bin."));
    box.setTextFormat(Qt::PlainText);
    QPushButton* remove = box.addButton(tr("Move to Recycle Bin"), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() != remove)
        return;

    // Only the Recycle Bin: when trashing fails the file stays where it is (never a permanent delete).
    if (!FS::trash(entry.path)) {
        ZeroToast::show(window(), tr("Could not move %1 to the Recycle Bin. The file was not deleted.").arg(entry.fileName),
                        ZeroToast::Kind::Error);
        return;
    }
    ZeroToast::show(window(), tr("Moved %1 to the Recycle Bin").arg(entry.fileName), ZeroToast::Kind::Success);

    // move on to the next image (the previous one when it was the last), close when it was the only one
    const int shown = m_index;
    m_entries.removeAt(shown);
    if (m_entries.isEmpty()) {
        closeViewer();
    } else {
        showIndex(qMin(shown, int(m_entries.size()) - 1));
    }
    emit fileTrashed(entry.path);
}

}  // namespace Zero
