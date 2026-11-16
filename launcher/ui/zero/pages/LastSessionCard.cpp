// SPDX-License-Identifier: GPL-3.0-only
#include "LastSessionCard.h"

#include <QClipboard>
#include <QEnterEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>
#include "ElidedLabel.h"
#include "ThumbnailCache.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"

namespace Zero {

namespace {

constexpr int kThumbW = 128;
constexpr int kThumbH = 72;

/// One screenshot of the session: cover-cropped thumbnail, a Copy button while hovered or focused.
class ShotThumb : public QWidget {
   public:
    ShotThumb(const QString& path, ThumbnailCache* cache, QWidget* parent) : QWidget(parent), m_path(path), m_cache(cache)
    {
        setFixedSize(kThumbW, kThumbH);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setToolTip(QFileInfo(path).fileName());
        m_modified = QFileInfo(path).lastModified().toMSecsSinceEpoch();

        m_copy = new QToolButton(this);
        m_copy->setFixedSize(Tokens::controlHeight - 8, Tokens::controlHeight - 8);
        m_copy->setIconSize(QSize(14, 14));
        m_copy->setToolTip(QObject::tr("Copy"));
        Style::setProperty(m_copy, "zeroVariant", "secondary");
        m_copy->move(kThumbW - m_copy->width() - Tokens::spaceXs, kThumbH - m_copy->height() - Tokens::spaceXs);
        m_copy->hide();
        QObject::connect(m_copy, &QToolButton::clicked, this, [this]() {
            QGuiApplication::clipboard()->setImage(QImage(m_path));
            ZeroToast::show(window(), QObject::tr("Screenshot copied"), ZeroToast::Kind::Success);
        });
        QObject::connect(cache, &ThumbnailCache::thumbnailReady, this, [this](const QString& p) {
            if (p == m_path)
                update();
        });
        auto refresh = [this]() { m_copy->setIcon(Icons::icon(QStringLiteral("copy"), Theme::current().text)); };
        refresh();
        QObject::connect(&Theme::instance(), &Theme::changed, this, refresh);
    }

    std::function<void(const QString&)> onOpen;

   protected:
    void enterEvent(QEnterEvent* e) override
    {
        QWidget::enterEvent(e);
        m_copy->show();
    }
    void leaveEvent(QEvent* e) override
    {
        QWidget::leaveEvent(e);
        m_copy->hide();
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && onOpen)
            onOpen(m_path);
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()), t.radiusMd, t.radiusMd);
        p.setClipPath(clip);
        p.fillRect(rect(), t.surfaceHover);
        const QPixmap pm = m_cache->thumbnail(m_path, m_modified);
        if (!pm.isNull()) {
            // cover crop around the centre
            const QSizeF src = pm.size().toSizeF();
            const qreal scale = std::max(kThumbW / src.width(), kThumbH / src.height());
            const QSizeF crop(kThumbW / scale, kThumbH / scale);
            const QRectF source((src.width() - crop.width()) / 2, (src.height() - crop.height()) / 2, crop.width(), crop.height());
            p.drawPixmap(QRectF(rect()), pm, source);
        }
        p.setClipping(false);
        p.setPen(QPen(t.border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd, t.radiusMd);
    }

   private:
    QString m_path;
    ThumbnailCache* m_cache;
    qint64 m_modified = 0;
    QToolButton* m_copy;
};

/// The "+N" tile after the thumbnails.
class MoreTile : public QWidget {
   public:
    MoreTile(int extra, QWidget* parent) : QWidget(parent), m_extra(extra)
    {
        setFixedSize(kThumbW, kThumbH);
        setCursor(Qt::PointingHandCursor);
    }
    std::function<void()> onClick;

   protected:
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && onClick)
            onClick();
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(t.border, 1));
        p.setBrush(t.surfaceHover);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd, t.radiusMd);
        p.setPen(t.textSecondary);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("+%1").arg(m_extra));
    }

   private:
    int m_extra;
};

}  // namespace

LastSessionCard::LastSessionCard(const Session::LastSession& session, QWidget* parent)
    : ZeroCard(parent), m_session(session), m_cache(new ThumbnailCache(this))
{
    setHoverEnabled(false);
    setContentsMargins(0, 0, 0, 0);  // the layout below sets the padding
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
    col->setSpacing(Tokens::spaceMd);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    auto* texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(Tokens::spaceXs);
    auto* overline = new QLabel(tr("Last session"), this);
    Style::applyRole(overline, Role::Overline);
    texts->addWidget(overline);
    auto* name = new ElidedLabel(this);
    Style::applyRole(name, Role::Heading);
    name->setFullText(session.instanceName);
    texts->addWidget(name);
    auto* duration = new ElidedLabel(this);
    Style::applyRole(duration, Role::Caption);
    duration->setFullText(durationLine());
    texts->addWidget(duration);
    head->addLayout(texts, 1);
    m_close = new QToolButton(this);
    m_close->setFixedSize(Tokens::controlHeight, Tokens::controlHeight);
    m_close->setIconSize(QSize(14, 14));
    m_close->setToolTip(tr("Dismiss"));
    Style::setProperty(m_close, "zeroVariant", "ghost");
    connect(m_close, &QToolButton::clicked, this, &LastSessionCard::dismissed);
    head->addWidget(m_close, 0, Qt::AlignTop);
    col->addLayout(head);

    // screenshots taken during the session that are still there
    QStringList existing;
    for (const QString& path : session.screenshots) {
        if (QFileInfo::exists(path))
            existing << path;
    }
    if (!existing.isEmpty()) {
        auto* shots = new QHBoxLayout;
        shots->setContentsMargins(0, 0, 0, 0);
        shots->setSpacing(Tokens::spaceSm);
        // when everything fits the tiles are the newest ones; with more, the first six and "+N"
        const int shown = std::min<int>(kMaxThumbnails, int(existing.size()));
        for (int i = 0; i < shown; ++i) {
            auto* thumb = new ShotThumb(existing.at(i), m_cache, this);
            thumb->onOpen = [this](const QString& path) { emit screenshotRequested(path); };
            shots->addWidget(thumb);
        }
        m_thumbs = shown;
        m_extra = int(existing.size()) - shown;
        if (m_extra > 0) {
            auto* more = new MoreTile(m_extra, this);
            const QString id = session.instanceId;
            more->onClick = [this, id]() { emit moreRequested(id); };
            shots->addWidget(more);
        }
        shots->addStretch(1);
        col->addLayout(shots);
    }

    if (session.worldsBackedUp > 0) {
        auto* line = new QHBoxLayout;
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(Tokens::spaceSm);
        m_backupIcon = new QLabel(this);
        m_backupIcon->setFixedSize(16, 16);
        line->addWidget(m_backupIcon);
        m_backupLine = new QLabel(session.worldsBackedUp == 1 ? tr("World backed up") : tr("%1 worlds backed up").arg(session.worldsBackedUp), this);
        Style::applyRole(m_backupLine, Role::Caption);
        Style::setProperty(m_backupLine, "zeroTone", "secondary");
        line->addWidget(m_backupLine);
        line->addStretch(1);
        col->addLayout(line);
    }

    connect(&Theme::instance(), &Theme::changed, this, &LastSessionCard::refreshIcons);
    refreshIcons();
}

QString LastSessionCard::durationLine() const
{
    return tr("You played %1").arg(Format::playtime(m_session.durationSeconds()));
}

void LastSessionCard::refreshIcons()
{
    const auto& t = Theme::current();
    m_close->setIcon(Icons::icon(QStringLiteral("x"), t.textTertiary));
    if (m_backupIcon)
        m_backupIcon->setPixmap(Icons::pixmap(QStringLiteral("shield-check"), t.success, 16, devicePixelRatioF()));
}

}  // namespace Zero
