// SPDX-License-Identifier: GPL-3.0-only
#include "ContentDetailsPanel.h"

#include <QAbstractButton>
#include <QDesktopServices>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QPainterPath>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "ContentDraw.h"
#include "RemoteImageCache.h"
#include "ui/widgets/ProjectDescriptionPage.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTransition.h"
#include "ui/zero/pages/ElidedLabel.h"
#include "ui/zero/pages/FlowLayout.h"

namespace Zero::Content {

bool isOpenableProviderLink(const QUrl& url)
{
    const QString scheme = url.scheme().toLower();
    return url.isValid() && (scheme == QLatin1String("http") || scheme == QLatin1String("https"));
}

bool openProviderLink(const QUrl& url)
{
    return isOpenableProviderLink(url) && QDesktopServices::openUrl(url);
}

namespace {
constexpr int kPad = Tokens::spaceLg;

int tabIndex(ContentDetailsPanel::Tab tab)
{
    return int(tab);
}

QString dateText(const QDateTime& date)
{
    return date.isValid() ? QLocale(QLocale::English).toString(date.toLocalTime().date(), QStringLiteral("d MMM yyyy")) : QString();
}

QString typeLabel(ModpackVersion::Type type)
{
    switch (type) {
        case ModpackVersion::Type::Release:
            return QObject::tr("Release");
        case ModpackVersion::Type::Beta:
            return QObject::tr("Beta");
        case ModpackVersion::Type::Alpha:
            return QObject::tr("Alpha");
        default:
            return {};
    }
}

PillTag::Tone typeTone(ModpackVersion::Type type)
{
    switch (type) {
        case ModpackVersion::Type::Release:
            return PillTag::Tone::Success;
        case ModpackVersion::Type::Beta:
            return PillTag::Tone::Warning;
        case ModpackVersion::Type::Alpha:
            return PillTag::Tone::Danger;
        default:
            return PillTag::Tone::Plain;
    }
}

void clearLayout(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
}

QString linkStyle()
{
    const auto& t = Theme::current();
    return QStringLiteral("a { color: %1; } body { color: %2; }").arg(t.accent2.isValid() ? t.accent2.name() : t.accent.name(), t.text.name());
}
}  // namespace

// ---- hero artwork with the icon overlapping it ----

class ContentDetailsPanel::HeroArt : public QWidget {
   public:
    HeroArt(Zero::RemoteImageCache* images, QWidget* parent) : QWidget(parent), m_images(images) { setFixedHeight(kHeroHeight + kIconSize / 2); }

    void setUrls(const QString& artwork, const QString& icon, bool compact, const QString& placeholder)
    {
        m_artwork = artwork;
        m_icon = icon;
        m_placeholder = placeholder.isEmpty() ? QStringLiteral("package") : placeholder;
        const int art = compact ? kCompactHeroHeight : kHeroHeight;
        if (art != m_artHeight) {
            m_artHeight = art;
            setFixedHeight(m_artHeight + kIconSize / 2);
        }
        update();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const qreal dpr = devicePixelRatioF();
        const QRect art(0, 0, width(), m_artHeight);
        // Top corners follow the panel's radius; the bottom of the artwork is square.
        QPainterPath clip = Paint::panelPath(QRectF(0, 0, width(), m_artHeight + 2 * t.radiusLg), t.radiusLg);
        p.save();
        p.setClipPath(clip);
        p.setClipRect(art, Qt::IntersectClip);
        QLinearGradient base(art.topLeft(), art.bottomRight());
        base.setColorAt(0, t.elevated);
        base.setColorAt(1, t.higher);
        p.fillRect(art, base);
        QPixmap picture;
        qreal fade = 0.0;
        if (!m_artwork.isEmpty() && m_images && !m_images->hasFailed(m_artwork)) {
            picture = m_images->request(m_artwork, Zero::RemoteImageCache::bucketFor(width(), dpr), this);
            fade = picture.isNull() ? 0.0 : m_images->fadeIn(m_artwork);
        }
        if ((picture.isNull() || fade < 1.0) && m_artHeight == kHeroHeight) {
            p.save();
            p.setOpacity(1.0 - fade);
            Icons::icon(m_placeholder, t.textTertiary).paint(&p, QRect(art.center().x() - 16, art.center().y() - 16, 32, 32));
            p.restore();
        }
        if (!picture.isNull()) {
            p.setOpacity(Motion::easeEnter().valueForProgress(fade));
            Draw::drawCover(&p, art, picture);
        }
        p.restore();

        // The icon: a ring in the panel's color, the icon, a hairline border.
        const QRect icon(kPad, m_artHeight - kIconSize / 2, kIconSize, kIconSize);
        p.setPen(Qt::NoPen);
        p.setBrush(t.surface);
        p.drawPath(Paint::panelPath(QRectF(icon).adjusted(-4, -4, 4, 4), t.radiusMd + 3));
        const QPainterPath shape = Paint::panelPath(QRectF(icon), t.radiusMd);
        p.setBrush(t.elevated);
        p.drawPath(shape);
        QPixmap pixmap;
        qreal iconFade = 0.0;
        if (!m_icon.isEmpty() && m_images && !m_images->hasFailed(m_icon)) {
            pixmap = m_images->request(m_icon, Zero::RemoteImageCache::bucketFor(kIconSize, dpr), this);
            iconFade = pixmap.isNull() ? 0.0 : m_images->fadeIn(m_icon);
        }
        if (pixmap.isNull() || iconFade < 1.0) {
            p.save();
            p.setOpacity(1.0 - iconFade);
            Icons::icon(m_placeholder, t.textTertiary).paint(&p, QRect(icon.center().x() - 12, icon.center().y() - 12, 24, 24));
            p.restore();
        }
        if (!pixmap.isNull()) {
            p.save();
            p.setOpacity(Motion::easeEnter().valueForProgress(iconFade));
            p.setClipPath(shape);
            Draw::drawCover(&p, icon, pixmap);
            p.restore();
        }
        p.setPen(QPen(t.border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(Paint::panelPath(QRectF(icon).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
    }

   private:
    Zero::RemoteImageCache* m_images;
    QString m_artwork;
    QString m_icon;
    QString m_placeholder = QStringLiteral("package");
    int m_artHeight = kHeroHeight;
};

// ---- the tab row ----

class ContentDetailsPanel::TabBar : public QWidget {
   public:
    explicit TabBar(QWidget* parent) : QWidget(parent) { setFixedHeight(36); }

    std::function<void(ContentDetailsPanel::Tab)> onSelected;

    void setTabs(const QList<QPair<ContentDetailsPanel::Tab, QString>>& tabs, ContentDetailsPanel::Tab current)
    {
        m_tabs = tabs;
        m_current = current;
        relayout();
        update();
    }
    QStringList labels() const
    {
        QStringList out;
        for (const auto& [tab, label] : m_tabs) {
            out.append(label);
        }
        return out;
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setPen(QPen(t.border, 1));
        p.drawLine(0, height() - 1, width(), height() - 1);
        const QFont font = Style::font(Role::Label);
        p.setFont(font);
        for (int i = 0; i < m_tabs.size(); ++i) {
            const QRect r = m_rects.value(i);
            const bool current = m_tabs[i].first == m_current;
            const bool hover = i == m_hover;
            p.setPen(current ? t.text : (hover ? t.text : t.textSecondary));
            p.drawText(r, Qt::AlignCenter, m_tabs[i].second);
            if (current) {
                p.fillRect(QRect(r.left(), height() - 2, r.width(), 2), t.accent2.isValid() ? t.accent2 : t.accent);
            }
        }
        if (hasFocus() && !m_rects.isEmpty()) {
            for (int i = 0; i < m_tabs.size(); ++i) {
                if (m_tabs[i].first == m_current) {
                    p.setPen(QPen(t.accent2.isValid() ? t.accent2 : t.accent, 1, Qt::DotLine));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(m_rects[i].adjusted(0, 4, 0, -4));
                }
            }
        }
    }
    void mousePressEvent(QMouseEvent* e) override { pick(e->position().toPoint()); }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        const int hover = indexAt(e->position().toPoint());
        if (hover != m_hover) {
            m_hover = hover;
            update();
        }
    }
    void leaveEvent(QEvent*) override
    {
        m_hover = -1;
        update();
    }
    void resizeEvent(QResizeEvent*) override { relayout(); }
    void keyPressEvent(QKeyEvent* e) override
    {
        int dir = 0;
        if (e->key() == Qt::Key_Right) {
            dir = 1;
        } else if (e->key() == Qt::Key_Left) {
            dir = -1;
        } else {
            QWidget::keyPressEvent(e);
            return;
        }
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (m_tabs[i].first == m_current) {
                const int next = i + dir;
                if (next >= 0 && next < m_tabs.size() && onSelected) {
                    onSelected(m_tabs[next].first);
                }
                break;
            }
        }
    }

   private:
    void relayout()
    {
        m_rects.clear();
        const QFontMetrics fm(Style::font(Role::Label));
        int x = kPad - 8;
        for (const auto& [tab, label] : m_tabs) {
            const int w = fm.horizontalAdvance(label) + 20;
            m_rects.append(QRect(x, 0, w, height()));
            x += w;
        }
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }
    int indexAt(const QPoint& pos) const
    {
        for (int i = 0; i < m_rects.size(); ++i) {
            if (m_rects[i].contains(pos)) {
                return i;
            }
        }
        return -1;
    }
    void pick(const QPoint& pos)
    {
        const int i = indexAt(pos);
        if (i >= 0 && onSelected) {
            onSelected(m_tabs[i].first);
        }
    }

    QList<QPair<ContentDetailsPanel::Tab, QString>> m_tabs;
    QList<QRect> m_rects;
    ContentDetailsPanel::Tab m_current = ContentDetailsPanel::Tab::Overview;
    int m_hover = -1;
};

// ---- the gallery thumbnails ----

class ContentDetailsPanel::ThumbGrid : public QWidget {
   public:
    ThumbGrid(Zero::RemoteImageCache* images, QWidget* parent) : QWidget(parent), m_images(images)
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void(int)> onActivated;

    void setEntries(const QList<GalleryEntry>& entries)
    {
        m_entries = entries;
        m_hover = -1;
        m_focus = entries.isEmpty() ? -1 : 0;
        setMinimumHeight(heightFor(width()));
        updateGeometry();
        update();
    }
    QSize sizeHint() const override { return { kWidth - 2 * kPad, heightFor(kWidth - 2 * kPad) }; }
    int heightFor(int w) const
    {
        const int rows = (int(m_entries.size()) + 1) / 2;
        const int tileH = tileSize(w).height();
        return rows == 0 ? 0 : rows * tileH + (rows - 1) * kGap;
    }

   protected:
    void resizeEvent(QResizeEvent*) override { setMinimumHeight(heightFor(width())); }
    void paintEvent(QPaintEvent* event) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const qreal dpr = devicePixelRatioF();
        for (int i = 0; i < m_entries.size(); ++i) {
            const QRect r = rectOf(i);
            if (!r.intersects(event->rect())) {
                continue;
            }
            const QPainterPath shape = Paint::panelPath(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd);
            p.setPen(Qt::NoPen);
            p.setBrush(t.elevated);
            p.drawPath(shape);
            QPixmap pm;
            qreal fade = 0.0;
            const QString& url = m_entries[i].thumbUrl.isEmpty() ? m_entries[i].url : m_entries[i].thumbUrl;
            if (m_images && !m_images->hasFailed(url)) {
                pm = m_images->request(url, Zero::RemoteImageCache::bucketFor(r.width(), dpr), this);
                fade = pm.isNull() ? 0.0 : m_images->fadeIn(url);
            }
            if (pm.isNull() || fade < 1.0) {
                p.save();
                p.setOpacity(1.0 - fade);
                Icons::icon(QStringLiteral("image"), t.textTertiary).paint(&p, QRect(r.center().x() - 10, r.center().y() - 10, 20, 20));
                p.restore();
            }
            if (!pm.isNull()) {
                p.save();
                p.setClipPath(shape);
                p.setOpacity(Motion::easeEnter().valueForProgress(fade));
                Draw::drawCover(&p, r, pm);
                p.restore();
            }
            const bool hot = i == m_hover || (hasFocus() && i == m_focus);
            p.setPen(QPen(hot ? t.accent : t.border, 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(shape);
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        const int i = indexAt(e->position().toPoint());
        if (i != m_hover) {
            m_hover = i;
            update();
        }
    }
    void leaveEvent(QEvent*) override
    {
        m_hover = -1;
        update();
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        const int i = indexAt(e->position().toPoint());
        if (i >= 0 && onActivated) {
            onActivated(i);
        }
    }
    void keyPressEvent(QKeyEvent* e) override
    {
        switch (e->key()) {
            case Qt::Key_Left:
                m_focus = std::max(0, m_focus - 1);
                break;
            case Qt::Key_Right:
                m_focus = std::min(int(m_entries.size()) - 1, m_focus + 1);
                break;
            case Qt::Key_Up:
                m_focus = std::max(0, m_focus - 2);
                break;
            case Qt::Key_Down:
                m_focus = std::min(int(m_entries.size()) - 1, m_focus + 2);
                break;
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Space:
                if (m_focus >= 0 && onActivated) {
                    onActivated(m_focus);
                }
                return;
            default:
                QWidget::keyPressEvent(e);
                return;
        }
        update();
    }

   private:
    static constexpr int kGap = Tokens::spaceSm;
    static QSize tileSize(int w)
    {
        const int tw = std::max(40, (w - kGap) / 2);
        return { tw, tw * 9 / 16 };
    }
    QRect rectOf(int i) const
    {
        const QSize s = tileSize(width());
        return QRect((i % 2) * (s.width() + kGap), (i / 2) * (s.height() + kGap), s.width(), s.height());
    }
    int indexAt(const QPoint& pos) const
    {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (rectOf(i).contains(pos)) {
                return i;
            }
        }
        return -1;
    }

    Zero::RemoteImageCache* m_images;
    QList<GalleryEntry> m_entries;
    int m_hover = -1;
    int m_focus = -1;
};

// ---- one row of the Versions tab ----

class ContentDetailsPanel::VersionRow : public QWidget {
   public:
    VersionRow(const ModpackVersion& version, QWidget* parent) : QWidget(parent), m_version(version)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        m_header = new Header(version, this);
        layout->addWidget(m_header);
        m_body = new QLabel(this);
        m_body->setWordWrap(true);
        m_body->setTextFormat(Qt::RichText);
        m_body->setOpenExternalLinks(false);
        connect(m_body, &QLabel::linkActivated, this, [](const QString& link) { openProviderLink(QUrl(link)); });
        m_body->setTextInteractionFlags(Qt::TextBrowserInteraction);
        m_body->setContentsMargins(kPad, 4, kPad, 12);
        m_body->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_body->hide();
        layout->addWidget(m_body);
        m_header->onClicked = [this]() {
            if (onToggled) {
                onToggled();
            }
        };
        if (version.changelogLoaded) {
            setChangelog(version.changelog, false);
        }
    }

    std::function<void()> onToggled;

    const ModpackVersion& version() const { return m_version; }
    bool changelogLoaded() const { return m_loaded; }
    bool expanded() const { return m_expanded; }
    QString changelogText() const { return m_body->text(); }

    void setExpanded(bool expanded)
    {
        m_expanded = expanded;
        m_header->setExpanded(expanded);
        m_body->setVisible(expanded);
        if (expanded && !m_loaded && !m_failed) {
            m_body->setText(QStringLiteral("<span style=\"color:%1\">%2</span>").arg(Theme::current().textSecondary.name(), tr("Loading changelog…")));
        }
    }
    void setChangelog(const QString& html, bool failed)
    {
        m_failed = failed;
        m_loaded = !failed;
        const auto& t = Theme::current();
        if (failed) {
            m_body->setText(QStringLiteral("<span style=\"color:%1\">%2</span>").arg(t.danger.name(), tr("Couldn't load the changelog.")));
        } else if (html.trimmed().isEmpty()) {
            m_body->setText(QStringLiteral("<span style=\"color:%1\">%2</span>").arg(t.textSecondary.name(), tr("No changelog was provided.")));
        } else {
            m_body->setText(html);
        }
    }
    /// A failed changelog is asked for again when the row is reopened.
    bool needsChangelog() const { return !m_loaded; }
    void resetFailed() { m_failed = false; }

   private:
    class Header : public QAbstractButton {
       public:
        Header(const ModpackVersion& v, QWidget* parent) : QAbstractButton(parent), m_v(v)
        {
            setCursor(Qt::PointingHandCursor);
            setFocusPolicy(Qt::StrongFocus);
            setAttribute(Qt::WA_Hover, true);
            setFixedHeight(52);
            setAccessibleName(v.name);
            connect(this, &QAbstractButton::clicked, this, [this]() {
                if (onClicked) {
                    onClicked();
                }
            });
        }
        std::function<void()> onClicked;
        void setExpanded(bool e)
        {
            m_expanded = e;
            update();
        }

       protected:
        void paintEvent(QPaintEvent*) override
        {
            const auto& t = Theme::current();
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, true);
            const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
            p.setPen(QPen(underMouse() || hasFocus() ? t.borderStrong : t.border, 1));
            p.setBrush(underMouse() ? t.surfaceHover : t.surface);
            p.drawPath(Paint::panelPath(r, t.radiusMd));
            const int chevron = 16;
            const int right = width() - kPad / 2 - chevron;
            Icons::icon(QStringLiteral("chevron-down"), t.textSecondary)
                .paint(&p, QRect(right, (height() - chevron) / 2, chevron, chevron), Qt::AlignCenter, m_expanded ? QIcon::Active : QIcon::Normal);
            // line 1: name + type pill
            const QFont nameFont = Style::font(Role::Label);
            const QFontMetrics nm(nameFont);
            const QString type = typeLabel(m_v.type);
            const int pillW = type.isEmpty() ? 0 : PillTag::widthForText(type) + 6;
            const int nameW = std::max(40, right - 8 - kPad / 2 - 10 - pillW);
            const QString name = nm.elidedText(m_v.name, Qt::ElideRight, nameW);
            p.setFont(nameFont);
            p.setPen(t.text);
            const int top = 8;
            p.drawText(QRect(10, top, nameW, nm.height()), Qt::AlignLeft | Qt::AlignVCenter, name);
            if (!type.isEmpty()) {
                PillTag::paintPill(&p, QRectF(10 + nm.horizontalAdvance(name) + 6, top + (nm.height() - 16) / 2, pillW - 6, 16), type,
                                   typeTone(m_v.type));
            }
            // line 2: Minecraft, loaders, date, size
            QStringList facts;
            if (const QString mc = minecraftRangeText(m_v.gameVersions); !mc.isEmpty()) {
                facts << mc;
            }
            QStringList loaders;
            for (const auto& l : m_v.loaders) {
                const QStringList label = loaderLabels({ l });
                if (!label.isEmpty()) {
                    loaders << label.first();
                }
            }
            if (!loaders.isEmpty()) {
                facts << loaders.join(QStringLiteral(" / "));
            }
            if (const QString d = dateText(m_v.date); !d.isEmpty()) {
                facts << d;
            }
            if (m_v.size >= 0) {
                facts << Format::bytes(m_v.size);
            }
            if (m_v.recommended) {
                facts.prepend(tr("Recommended"));
            }
            const QFont capFont = Style::font(Role::Caption);
            const QFontMetrics cm(capFont);
            p.setFont(capFont);
            p.setPen(t.textSecondary);
            p.drawText(QRect(10, top + nm.height() + 4, right - 18, cm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       cm.elidedText(facts.join(QStringLiteral("  ·  ")), Qt::ElideRight, right - 18));
        }

       private:
        ModpackVersion m_v;
        bool m_expanded = false;
    };

    ModpackVersion m_version;
    Header* m_header;
    QLabel* m_body;
    bool m_expanded = false;
    bool m_loaded = false;
    bool m_failed = false;
};

// ---- the panel ----

ContentDetailsPanel::ContentDetailsPanel(Zero::RemoteImageCache* images, QWidget* parent)
    : QWidget(parent)
    , m_images(images)
    , m_body(new QWidget(this))
    , m_effect(new QGraphicsOpacityEffect(this))
{
    setFixedWidth(kWidth);
    setObjectName(QStringLiteral("zeroDetailsPanel"));
    hide();

    auto* root = new QVBoxLayout(m_body);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_hero = new HeroArt(m_images, m_body);
    root->addWidget(m_hero);
    m_close = new QToolButton(m_hero);
    Style::setProperty(m_close, "zeroVariant", "secondary");
    m_close->setIconSize(QSize(14, 14));
    m_close->setFixedSize(28, 28);
    m_close->setToolTip(tr("Close details"));
    m_close->setAccessibleName(tr("Close details"));
    m_close->setCursor(Qt::PointingHandCursor);
    m_close->move(kWidth - 28 - 10, 10);
    auto refreshIcons = [this]() {
        m_close->setIcon(Icons::icon(QStringLiteral("x"), Theme::current().text));
        m_website->setIcon(Icons::icon(QStringLiteral("external-link"), Theme::current().text));
    };
    connect(m_close, &QToolButton::clicked, this, [this]() {
        clear();
        emit closeRequested();
    });

    auto* head = new QWidget(m_body);
    auto* headLayout = new QVBoxLayout(head);
    headLayout->setContentsMargins(kPad, 4, kPad, 0);
    headLayout->setSpacing(Tokens::spaceSm);
    auto* name = new ElidedLabel(head);
    m_name = name;
    Style::applyRole(m_name, Role::Title);
    headLayout->addWidget(m_name);
    m_author = new QLabel(head);
    Style::applyRole(m_author, Role::Caption);
    m_author->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::current().textSecondary.name()));
    headLayout->addWidget(m_author);
    m_pills = new QWidget(head);
    new FlowLayout(m_pills, 6, 6);
    headLayout->addWidget(m_pills);
    m_stats = new QWidget(head);
    new FlowLayout(m_stats, 14, 4);
    headLayout->addWidget(m_stats);

    auto* actions = new QWidget(head);
    auto* actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 2, 0, 0);
    actionsLayout->setSpacing(Tokens::spaceSm);
    m_primary = new QPushButton(tr("Install"), actions);
    Style::setProperty(m_primary, "zeroVariant", "primary");
    Style::setProperty(m_primary, "zeroSize", "lg");
    m_primary->setCursor(Qt::PointingHandCursor);
    m_cart = new QPushButton(tr("Add to cart"), head);  // its own row: primary + cart + Website do not fit 400 px
    Style::setProperty(m_cart, "zeroVariant", "secondary");
    Style::setProperty(m_cart, "zeroSize", "lg");
    m_cart->setIconSize(QSize(16, 16));
    m_cart->setCursor(Qt::PointingHandCursor);
    m_cart->hide();
    m_website = new QPushButton(tr("Website"), actions);
    Style::setProperty(m_website, "zeroVariant", "ghost");
    Style::setProperty(m_website, "zeroSize", "lg");
    m_website->setIconSize(QSize(16, 16));
    m_website->setCursor(Qt::PointingHandCursor);
    actionsLayout->addWidget(m_primary, 1);
    actionsLayout->addWidget(m_website);
    headLayout->addWidget(actions);
    headLayout->addWidget(m_cart);
    m_note = new QLabel(head);
    m_note->setWordWrap(true);
    Style::applyRole(m_note, Role::Caption);
    m_note->hide();
    headLayout->addWidget(m_note);
    root->addWidget(head);
    connect(m_primary, &QPushButton::clicked, this, &ContentDetailsPanel::primaryActionRequested);
    connect(m_cart, &QPushButton::clicked, this, &ContentDetailsPanel::cartToggleRequested);
    connect(m_website, &QPushButton::clicked, this, &ContentDetailsPanel::websiteRequested);

    root->addSpacing(Tokens::spaceSm);
    m_tabs = new TabBar(m_body);
    m_tabs->onSelected = [this](Tab tab) { setCurrentTab(tab); };
    root->addWidget(m_tabs);

    m_pages = new QStackedWidget(m_body);
    root->addWidget(m_pages, 1);

    // Overview
    m_overviewPage = new QWidget(m_pages);
    auto* overviewLayout = new QVBoxLayout(m_overviewPage);
    overviewLayout->setContentsMargins(kPad, Tokens::spaceMd, kPad, Tokens::spaceMd);
    overviewLayout->setSpacing(Tokens::spaceSm);
    m_overviewStatus = new QLabel(m_overviewPage);
    m_overviewStatus->setWordWrap(true);
    Style::applyRole(m_overviewStatus, Role::Caption);
    m_overviewRetry = new QPushButton(tr("Retry"), m_overviewPage);
    Style::setProperty(m_overviewRetry, "zeroVariant", "secondary");
    m_overview = new ProjectDescriptionPage(m_overviewPage);
    m_overview->setMetaEntry(QStringLiteral("ZeroContent"));
    m_overview->setOpenLinks(false);
    m_overview->setOpenExternalLinks(false);
    connect(m_overview, &QTextBrowser::anchorClicked, this, [](const QUrl& url) { openProviderLink(url); });
    m_overview->setFrameShape(QFrame::NoFrame);
    m_overview->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; }"));
    m_overview->setAccessibleName(tr("Description"));
    overviewLayout->addWidget(m_overviewStatus);
    overviewLayout->addWidget(m_overviewRetry, 0, Qt::AlignLeft);
    overviewLayout->addWidget(m_overview, 1);
    m_overviewRetry->hide();
    connect(m_overviewRetry, &QPushButton::clicked, this, [this]() { emit retryRequested(Tab::Overview); });
    m_pages->addWidget(m_overviewPage);

    // Gallery
    m_galleryScroll = new QScrollArea(m_pages);
    m_galleryScroll->setFrameShape(QFrame::NoFrame);
    m_galleryScroll->setWidgetResizable(true);
    m_galleryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_galleryScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    m_galleryScroll->viewport()->setAutoFillBackground(false);
    auto* galleryHost = new QWidget;
    galleryHost->setAutoFillBackground(false);
    auto* galleryLayout = new QVBoxLayout(galleryHost);
    galleryLayout->setContentsMargins(kPad, Tokens::spaceMd, kPad, Tokens::spaceMd);
    m_thumbs = new ThumbGrid(m_images, galleryHost);
    galleryLayout->addWidget(m_thumbs);
    galleryLayout->addStretch(1);
    m_galleryScroll->setWidget(galleryHost);
    m_thumbs->onActivated = [this](int index) { openGallery(index); };
    m_pages->addWidget(m_galleryScroll);

    // Versions
    m_versionsScroll = new QScrollArea(m_pages);
    m_versionsScroll->setFrameShape(QFrame::NoFrame);
    m_versionsScroll->setWidgetResizable(true);
    m_versionsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_versionsScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    m_versionsScroll->viewport()->setAutoFillBackground(false);
    m_versionsBody = new QWidget;
    m_versionsBody->setAutoFillBackground(false);
    m_versionsLayout = new QVBoxLayout(m_versionsBody);
    m_versionsLayout->setContentsMargins(kPad, Tokens::spaceMd, kPad, Tokens::spaceMd);
    m_versionsLayout->setSpacing(Tokens::spaceSm);
    m_versionsScroll->setWidget(m_versionsBody);
    m_pages->addWidget(m_versionsScroll);

    // What's new
    auto* whatsNewPage = new QWidget(m_pages);
    auto* whatsNewLayout = new QVBoxLayout(whatsNewPage);
    whatsNewLayout->setContentsMargins(kPad, Tokens::spaceMd, kPad, Tokens::spaceMd);
    m_whatsNew = new QTextBrowser(whatsNewPage);
    m_whatsNew->setOpenLinks(false);
    m_whatsNew->setOpenExternalLinks(false);
    connect(m_whatsNew, &QTextBrowser::anchorClicked, this, [](const QUrl& url) { openProviderLink(url); });
    m_whatsNew->setFrameShape(QFrame::NoFrame);
    m_whatsNew->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; }"));
    m_whatsNew->setAccessibleName(tr("What's new"));
    whatsNewLayout->addWidget(m_whatsNew);
    m_pages->addWidget(whatsNewPage);

    m_viewer = new ContentGalleryViewer(m_images, this);
    connect(m_viewer, &ContentGalleryViewer::closed, this, [this]() { m_thumbs->setFocus(Qt::OtherFocusReason); });

    refreshIcons();
    connect(&Theme::instance(), &Theme::changed, this, [this, refreshIcons]() {
        refreshIcons();
        m_author->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::current().textSecondary.name()));
        m_note->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::current().textSecondary.name()));
    });
    m_note->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::current().textSecondary.name()));

    m_body->setGraphicsEffect(m_effect);
    m_effect->setEnabled(false);
    m_slide.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_slide, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const qreal p = v.toReal();
        m_bodyOffset = qRound((1.0 - p) * kSlidePx);
        m_effect->setOpacity(p);
        layoutBody();
    });
    connect(&m_slide, &QVariantAnimation::finished, this, [this]() {
        m_bodyOffset = 0;
        m_effect->setEnabled(false);
        layoutBody();
    });
    refreshTabs();
}

void ContentDetailsPanel::refreshImages()
{
    m_hero->update();
    m_thumbs->update();
    if (m_viewer->isOpen()) {
        m_viewer->update();
    }
}

void ContentDetailsPanel::setOverlayHost(QWidget* host)
{
    m_viewer->setHost(host ? host : this);
}

void ContentDetailsPanel::layoutBody()
{
    m_body->setGeometry(m_bodyOffset, 0, width(), height());
}

void ContentDetailsPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutBody();
}

void ContentDetailsPanel::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(t.border, 1));
    p.setBrush(t.surface);
    p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
}

void ContentDetailsPanel::slideIn()
{
    m_slide.stop();
    const int ms = Motion::duration(kSlideMs);
    if (ms <= 0) {
        m_bodyOffset = 0;
        m_effect->setEnabled(false);
        layoutBody();
        return;
    }
    m_effect->setEnabled(true);
    m_effect->setOpacity(0.0);
    m_bodyOffset = kSlidePx;
    layoutBody();
    m_slide.setDuration(ms);
    m_slide.setStartValue(0.0);
    m_slide.setEndValue(1.0);
    m_slide.start();
}

void ContentDetailsPanel::applyHeader(const DetailsHeader& h)
{
    m_header = h;
    m_key = h.key;
    m_hero->setUrls(h.artworkUrl, h.iconUrl, h.compactHero, h.placeholderIcon);
    static_cast<ElidedLabel*>(m_name)->setFullText(h.name);
    m_name->setToolTip(h.name);
    m_name->setAccessibleName(h.name);
    m_author->setText(h.author.isEmpty() ? QString() : tr("by %1").arg(h.author));
    m_author->setVisible(!h.author.isEmpty());

    auto* pills = m_pills->layout();
    clearLayout(pills);
    QStringList texts;
    if (!h.providerLabel.isEmpty()) {
        texts << h.providerLabel;
    }
    texts << h.pills;
    for (const auto& text : std::as_const(texts)) {
        pills->addWidget(new PillTag(text, PillTag::Tone::Plain, m_pills));
    }
    m_pills->setVisible(!texts.isEmpty());

    auto* stats = m_stats->layout();
    clearLayout(stats);
    const auto& t = Theme::current();
    for (const auto& stat : h.stats) {
        auto* item = new QWidget(m_stats);
        auto* row = new QHBoxLayout(item);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto* icon = new QLabel(item);
        icon->setPixmap(Icons::pixmap(stat.icon, t.textSecondary, 14, devicePixelRatioF()));
        icon->setFixedSize(14, 14);
        auto* text = new QLabel(stat.text, item);
        Style::applyRole(text, Role::Caption);
        text->setStyleSheet(QStringLiteral("color: %1;").arg(t.textSecondary.name()));
        row->addWidget(icon);
        row->addWidget(text);
        item->setToolTip(stat.tooltip);
        item->setAccessibleName(stat.tooltip.isEmpty() ? stat.text : stat.tooltip + QStringLiteral(": ") + stat.text);
        stats->addWidget(item);
    }
    m_stats->setVisible(!h.stats.isEmpty());
    m_website->setVisible(!h.websiteUrl.isEmpty());
}

void ContentDetailsPanel::resetTabs()
{
    m_overviewState = LoadState::Loading;
    m_overviewHtml.clear();
    m_overviewStatus->setText(tr("Loading the description…"));
    m_overviewStatus->show();
    m_overviewRetry->hide();
    m_overview->clear();
    m_gallery.clear();
    m_thumbs->setEntries({});
    m_versionsState = LoadState::Loading;
    m_versionsError.clear();
    m_versions.clear();
    m_versionsShown = 0;
    m_whatsNewHtml.clear();
    m_whatsNew->clear();
    m_viewer->closeViewer();
    m_tab = Tab::Overview;
    rebuildVersions();
    m_note->hide();
    refreshTabs();
}

void ContentDetailsPanel::showItem(const DetailsHeader& header)
{
    const bool wasOpen = isOpen();
    if (!wasOpen) {
        applyHeader(header);
        resetTabs();
        m_primary->setText(tr("Install"));
        m_primary->setEnabled(false);
        m_cart->hide();
        show();
        layoutBody();
        slideIn();
        return;
    }
    if (header.key == m_key) {
        applyHeader(header);  // same item, fresher data: no crossfade
        return;
    }
    m_slide.stop();
    m_effect->setEnabled(false);
    m_bodyOffset = 0;
    layoutBody();
    Transition::run(m_body, Transition::Kind::Crossfade, kSwitchMs, [this, &header]() {
        applyHeader(header);
        resetTabs();
        m_primary->setEnabled(false);
        m_cart->hide();
    });
}

void ContentDetailsPanel::clear()
{
    m_slide.stop();
    m_effect->setEnabled(false);
    m_bodyOffset = 0;
    m_key.clear();
    m_viewer->closeViewer();
    hide();
}

void ContentDetailsPanel::setPrimaryAction(const QString& text, bool enabled, const QString& note)
{
    m_primary->setText(text);
    m_primary->setEnabled(enabled);
    m_primary->setAccessibleName(m_header.name.isEmpty() ? text : tr("%1 %2").arg(text, m_header.name));
    m_note->setText(note);
    m_note->setVisible(!note.isEmpty());
}

void ContentDetailsPanel::setCartAction(bool visible, bool inCart)
{
    m_cart->setVisible(visible);
    m_cart->setText(inCart ? tr("In cart") : tr("Add to cart"));
    m_cart->setIcon(inCart ? Icons::icon(QStringLiteral("check"), Theme::current().text) : QIcon());
    m_cart->setAccessibleName(m_header.name.isEmpty() ? m_cart->text() : tr("%1: %2").arg(m_cart->text(), m_header.name));
}

void ContentDetailsPanel::setOverview(LoadState state, const QString& html, const QString& error)
{
    m_overviewState = state;
    m_overviewHtml = html;
    switch (state) {
        case LoadState::Loading:
            m_overviewStatus->setText(tr("Loading the description…"));
            m_overviewStatus->show();
            m_overviewRetry->hide();
            if (html.isEmpty()) {
                m_overview->clear();
            }
            break;
        case LoadState::Failed:
            m_overviewStatus->setText(error.isEmpty() ? tr("Couldn't load the description.") : tr("Couldn't load the description: %1").arg(error));
            m_overviewStatus->show();
            m_overviewRetry->show();
            // What the search already told us (the short description) stays readable under the error.
            if (html.isEmpty()) {
                m_overview->clear();
            }
            break;
        default:
            m_overviewStatus->hide();
            m_overviewRetry->hide();
            break;
    }
    if (state == LoadState::Ready || (state != LoadState::None && !html.isEmpty())) {
        m_overview->document()->setDefaultStyleSheet(linkStyle());
        m_overview->setHtml(html);
        m_overview->flush();
        m_overview->verticalScrollBar()->setValue(0);
    }
    refreshTabs();
}

void ContentDetailsPanel::setGallery(const QList<GalleryEntry>& entries)
{
    m_gallery = entries;
    m_thumbs->setEntries(entries);
    refreshTabs();
}

void ContentDetailsPanel::setVersions(LoadState state, const QList<ModpackVersion>& versions, const QString& error)
{
    m_versionsState = state;
    m_versions = state == LoadState::Ready ? versions : QList<ModpackVersion>();
    m_versionsError = error;
    m_versionsShown = std::min(int(m_versions.size()), kVersionPage);
    rebuildVersions();
    refreshTabs();
}

void ContentDetailsPanel::rebuildVersions()
{
    clearLayout(m_versionsLayout);
    const auto& t = Theme::current();
    auto addStatus = [&](const QString& text, bool retry) {
        auto* label = new QLabel(text, m_versionsBody);
        label->setWordWrap(true);
        Style::applyRole(label, Role::Caption);
        label->setStyleSheet(QStringLiteral("color: %1;").arg(t.textSecondary.name()));
        m_versionsLayout->addWidget(label);
        if (retry) {
            auto* button = new QPushButton(tr("Retry"), m_versionsBody);
            Style::setProperty(button, "zeroVariant", "secondary");
            connect(button, &QPushButton::clicked, this, [this]() { emit retryRequested(Tab::Versions); });
            m_versionsLayout->addWidget(button, 0, Qt::AlignLeft);
        }
    };
    switch (m_versionsState) {
        case LoadState::Loading:
            m_versionsLayout->addWidget(new ZeroSkeleton(ZeroSkeleton::Shape::Rows, tr("Loading versions…"), m_versionsBody));
            m_versionsLayout->itemAt(0)->widget()->setMinimumHeight(220);
            break;
        case LoadState::Failed:
            addStatus(m_versionsError.isEmpty() ? tr("Couldn't load the versions.") : tr("Couldn't load the versions: %1").arg(m_versionsError), true);
            break;
        case LoadState::Ready:
            if (m_versions.isEmpty()) {
                addStatus(tr("This provider lists no downloadable versions for this pack."), false);
            }
            for (int i = 0; i < m_versionsShown; ++i) {
                auto* row = new VersionRow(m_versions.at(i), m_versionsBody);
                row->onToggled = [this, i]() { toggleVersion(i); };
                m_versionsLayout->addWidget(row);
            }
            if (m_versionsShown < m_versions.size()) {
                auto* more = new QPushButton(tr("Show all %1 versions").arg(m_versions.size()), m_versionsBody);
                Style::setProperty(more, "zeroVariant", "ghost");
                connect(more, &QPushButton::clicked, this, [this]() {
                    m_versionsShown = int(m_versions.size());
                    rebuildVersions();
                });
                m_versionsLayout->addWidget(more, 0, Qt::AlignLeft);
            }
            break;
        default:
            break;
    }
    m_versionsLayout->addStretch(1);
}

int ContentDetailsPanel::versionRowCount() const
{
    int n = 0;
    for (int i = 0; i < m_versionsLayout->count(); ++i) {
        if (dynamic_cast<VersionRow*>(m_versionsLayout->itemAt(i)->widget())) {
            ++n;
        }
    }
    return n;
}

void ContentDetailsPanel::toggleVersion(int row)
{
    VersionRow* target = nullptr;
    for (int i = 0, n = 0; i < m_versionsLayout->count(); ++i) {
        if (auto* r = dynamic_cast<VersionRow*>(m_versionsLayout->itemAt(i)->widget())) {
            if (n++ == row) {
                target = r;
                break;
            }
        }
    }
    if (!target) {
        return;
    }
    const bool open = !target->expanded();
    target->setExpanded(open);
    if (open && target->needsChangelog()) {
        target->resetFailed();
        emit changelogRequested(target->version().id);
    }
}

bool ContentDetailsPanel::isVersionExpanded(int row) const
{
    for (int i = 0, n = 0; i < m_versionsLayout->count(); ++i) {
        if (auto* r = dynamic_cast<VersionRow*>(m_versionsLayout->itemAt(i)->widget())) {
            if (n++ == row) {
                return r->expanded();
            }
        }
    }
    return false;
}

QString ContentDetailsPanel::versionChangelogText(int row) const
{
    for (int i = 0, n = 0; i < m_versionsLayout->count(); ++i) {
        if (auto* r = dynamic_cast<VersionRow*>(m_versionsLayout->itemAt(i)->widget())) {
            if (n++ == row) {
                return r->changelogText();
            }
        }
    }
    return {};
}

void ContentDetailsPanel::setChangelog(const QString& versionId, const QString& html, bool failed)
{
    for (int i = 0; i < m_versionsLayout->count(); ++i) {
        if (auto* r = dynamic_cast<VersionRow*>(m_versionsLayout->itemAt(i)->widget())) {
            if (r->version().id == versionId) {
                r->setChangelog(html, failed);
            }
        }
    }
    for (auto& v : m_versions) {
        if (v.id == versionId && !failed) {
            v.changelog = html;
            v.changelogLoaded = true;
        }
    }
}

void ContentDetailsPanel::setWhatsNew(const QString& html)
{
    m_whatsNewHtml = html;
    if (html.trimmed().isEmpty()) {
        m_whatsNew->clear();
    } else {
        m_whatsNew->document()->setDefaultStyleSheet(linkStyle());
        m_whatsNew->setHtml(html);
    }
    refreshTabs();
}

void ContentDetailsPanel::openGallery(int index)
{
    if (index < 0 || index >= m_gallery.size()) {
        return;
    }
    if (m_viewer->parentWidget() == this && !m_viewer->isOpen()) {
        // No host was given: cover the panel itself.
        m_viewer->setHost(this);
    }
    m_viewer->open(m_gallery, index);
}

bool ContentDetailsPanel::isTabVisible(Tab tab) const
{
    switch (tab) {
        case Tab::Overview:
            return m_overviewState == LoadState::Loading || m_overviewState == LoadState::Failed ||
                   (m_overviewState == LoadState::Ready && !m_overviewHtml.trimmed().isEmpty());
        case Tab::Gallery:
            return !m_gallery.isEmpty();
        case Tab::Versions:
            return m_versionsState != LoadState::None;
        case Tab::WhatsNew:
            return !m_whatsNewHtml.trimmed().isEmpty();
    }
    return false;
}

void ContentDetailsPanel::refreshTabs()
{
    QList<QPair<Tab, QString>> tabs;
    if (isTabVisible(Tab::Overview)) {
        tabs.append({ Tab::Overview, tr("Overview") });
    }
    if (isTabVisible(Tab::Gallery)) {
        tabs.append({ Tab::Gallery, tr("Gallery") });
    }
    if (isTabVisible(Tab::Versions)) {
        tabs.append({ Tab::Versions, tr("Versions") });
    }
    if (isTabVisible(Tab::WhatsNew)) {
        tabs.append({ Tab::WhatsNew, tr("What's new") });
    }
    // The current tab vanished (no data): fall back to the first visible one.
    if (!isTabVisible(m_tab) && !tabs.isEmpty()) {
        m_tab = tabs.first().first;
    }
    m_pages->setCurrentIndex(tabIndex(m_tab));
    m_tabs->setTabs(tabs, m_tab);
    m_tabs->setVisible(!tabs.isEmpty());
    m_pages->setVisible(!tabs.isEmpty());
}

void ContentDetailsPanel::setCurrentTab(Tab tab)
{
    if (!isTabVisible(tab) || tab == m_tab) {
        return;
    }
    m_tab = tab;
    refreshTabs();
    emit tabChanged(tab);
}

QStringList ContentDetailsPanel::visibleTabLabels() const
{
    return m_tabs->labels();
}

QString ContentDetailsPanel::nameText() const
{
    return m_header.name;
}

QString ContentDetailsPanel::authorText() const
{
    return m_author->text();
}

QString ContentDetailsPanel::noteText() const
{
    return m_note->isHidden() ? QString() : m_note->text();
}

QStringList ContentDetailsPanel::pillTexts() const
{
    QStringList out;
    for (auto* pill : m_pills->findChildren<PillTag*>()) {
        out.append(pill->text());
    }
    return out;
}

QStringList ContentDetailsPanel::statTexts() const
{
    QStringList out;
    const auto labels = m_stats->findChildren<QLabel*>();
    for (auto* label : labels) {
        if (!label->text().isEmpty()) {
            out.append(label->text());
        }
    }
    return out;
}

}  // namespace Zero::Content
