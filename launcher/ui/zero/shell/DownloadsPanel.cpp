// SPDX-License-Identifier: GPL-3.0-only
#include "DownloadsPanel.h"

#include <QFocusEvent>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/pages/ElidedLabel.h"
#include "zero/downloads/DownloadCenter.h"

namespace Zero {

namespace {

using Downloads::ActiveDownload;
using Downloads::DownloadResult;
using Downloads::HistoryEntry;
using Downloads::Kind;

constexpr int kMargin = Tokens::shadowBlur + Tokens::shadowOffsetY;  ///< transparent margin that holds the shadow
constexpr int kTile = 32;
constexpr int kTileIcon = 16;
constexpr int kCancelSize = 28;
constexpr int kShimmerMs = 1200;

QLabel* makeLabel(const QString& text, Role role, QWidget* parent, const char* tone = nullptr)
{
    auto* l = new QLabel(text, parent);
    Style::applyRole(l, role);
    if (tone)
        Style::setProperty(l, "zeroTone", tone);
    return l;
}

ElidedLabel* makeElided(const QString& text, Role role, QWidget* parent, const char* tone = nullptr)
{
    auto* l = new ElidedLabel(parent);
    Style::applyRole(l, role);
    if (tone)
        Style::setProperty(l, "zeroTone", tone);
    l->setFullText(text);
    return l;
}

/// Lucide icon of a kind (only what the bundle has).
QString iconNameOf(Kind kind)
{
    switch (kind) {
        case Kind::Mod:
            return QStringLiteral("puzzle");
        case Kind::ResourcePack:
            return QStringLiteral("images");
        case Kind::ShaderPack:
            return QStringLiteral("sun");
        case Kind::World:
            return QStringLiteral("earth");
        case Kind::Modpack:
            return QStringLiteral("package");
        case Kind::Mixed:
        case Kind::Update:
            break;
    }
    return QStringLiteral("download");
}

/// Shadow behind the panel: a plain panel shape carrying the popover shadow (mouse transparent).
class PopoverShadow : public QWidget {
   public:
    explicit PopoverShadow(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto* fx = new QGraphicsDropShadowEffect(this);
        fx->setBlurRadius(Tokens::shadowBlur);
        fx->setOffset(0, Tokens::shadowOffsetY);
        fx->setColor(QColor(0, 0, 0, Tokens::shadowAlpha));
        setGraphicsEffect(fx);
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::current().surfaceSolid());  // the popup is a window of its own: never see-through
        p.drawPath(Paint::panelPath(QRectF(rect()), Theme::current().radiusLg));
    }
};

class PopoverPanel : public QWidget {
   public:
    using QWidget::QWidget;

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(t.border, 1));
        p.setBrush(t.surfaceSolid());
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
    }
};

/// 32 px rounded tile with the kind icon.
class KindTile : public QWidget {
   public:
    KindTile(Kind kind, QWidget* parent) : QWidget(parent), m_icon(iconNameOf(kind))
    {
        setFixedSize(kTile, kTile);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(t.elevated);
        p.drawPath(Paint::panelPath(QRectF(rect()), t.radiusMd));
        Icons::icon(m_icon, t.textSecondary).paint(&p, QRect((width() - kTileIcon) / 2, (height() - kTileIcon) / 2, kTileIcon, kTileIcon));
    }

   private:
    QString m_icon;
};

/// 4 px progress bar: accent fill on the `elevated` track; an unknown fraction shimmers (a static segment with reduced motion).
class DownloadBar : public QWidget {
   public:
    explicit DownloadBar(QWidget* parent) : QWidget(parent), m_anim(new QVariantAnimation(this))
    {
        setObjectName(QStringLiteral("zeroDownloadBar"));
        setFixedHeight(Tokens::progressHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setProperty("zeroFraction", -1.0);
        m_anim->setStartValue(0.0);
        m_anim->setEndValue(1.0);
        m_anim->setDuration(kShimmerMs);
        m_anim->setEasingCurve(QEasingCurve::InOutSine);
        m_anim->setLoopCount(-1);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_phase = v.toReal();
            update();
        });
        connect(&Theme::instance(), &Theme::changed, this, [this]() {
            sync();
            update();
        });
    }

    void setFraction(double fraction)
    {
        if (fraction == m_fraction)
            return;
        m_fraction = fraction;
        setProperty("zeroFraction", fraction);
        sync();
        update();
    }
    double fraction() const { return m_fraction; }

   protected:
    void showEvent(QShowEvent* e) override
    {
        QWidget::showEvent(e);
        sync();
    }
    void hideEvent(QHideEvent* e) override
    {
        QWidget::hideEvent(e);
        sync();
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF track = QRectF(rect());
        const qreal radius = track.height() / 2.0;
        p.setPen(Qt::NoPen);
        p.setBrush(t.elevated);
        p.drawRoundedRect(track, radius, radius);
        p.setBrush(t.accent);
        if (m_fraction >= 0) {
            const qreal w = std::max(track.height(), track.width() * std::clamp(m_fraction, 0.0, 1.0));
            p.drawRoundedRect(QRectF(0, 0, w, track.height()), radius, radius);
            return;
        }
        // unknown: a segment 40 % of the track long
        const qreal seg = track.width() * 0.4;
        const bool moving = m_anim->state() == QAbstractAnimation::Running;
        const qreal x = moving ? -seg + (track.width() + seg) * m_phase : (track.width() - seg) / 2.0;
        p.setClipPath([&]() {
            QPainterPath clip;
            clip.addRoundedRect(track, radius, radius);
            return clip;
        }());
        p.setBrush(withAlpha(t.accent, moving ? 1.0 : 0.6));
        p.drawRoundedRect(QRectF(x, 0, seg, track.height()), radius, radius);
    }

   private:
    void sync()
    {
        const bool run = isVisible() && m_fraction < 0 && Motion::enabled();
        if (run && m_anim->state() != QAbstractAnimation::Running)
            m_anim->start();
        else if (!run && m_anim->state() == QAbstractAnimation::Running)
            m_anim->stop();
    }

    QVariantAnimation* m_anim;
    double m_fraction = -1;
    double m_phase = 0;
};

/// A row of the Recent list. Clickable rows show a hover fill and a hand cursor and answer Enter / Space.
class HistoryRow : public QWidget {
   public:
    explicit HistoryRow(QWidget* parent) : QWidget(parent), m_hover(new Motion::StateAnimator(this))
    {
        setAttribute(Qt::WA_Hover, true);
        connect(m_hover, &Motion::StateAnimator::progressChanged, this, qOverload<>(&QWidget::update));
    }

    void setClickable(bool clickable)
    {
        m_clickable = clickable;
        setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setFocusPolicy(clickable ? Qt::StrongFocus : Qt::NoFocus);
        update();
    }
    bool clickable() const { return m_clickable; }
    std::function<void()> onClick;

   protected:
    void enterEvent(QEnterEvent* e) override
    {
        QWidget::enterEvent(e);
        m_hover->setActive(m_clickable);
    }
    void leaveEvent(QEvent* e) override
    {
        QWidget::leaveEvent(e);
        m_hover->setActive(false);
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        if (m_clickable && e->button() == Qt::LeftButton) {
            m_pressed = true;
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (m_pressed && e->button() == Qt::LeftButton) {
            m_pressed = false;
            if (rect().contains(e->position().toPoint()) && onClick)
                onClick();
            e->accept();
            return;
        }
        QWidget::mouseReleaseEvent(e);
    }
    void keyPressEvent(QKeyEvent* e) override
    {
        if (m_clickable && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space)) {
            if (onClick)
                onClick();
            return;
        }
        QWidget::keyPressEvent(e);
    }
    void focusInEvent(QFocusEvent* e) override
    {
        m_keyboardFocus = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason;
        QWidget::focusInEvent(e);
        update();
    }
    void focusOutEvent(QFocusEvent* e) override
    {
        m_keyboardFocus = false;
        QWidget::focusOutEvent(e);
        update();
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal h = m_hover->progress();
        if (h > 0.0) {
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(t.surfaceHoverSolid(), h));
            p.drawPath(Paint::panelPath(QRectF(rect()), t.radiusMd));
        }
        if (m_keyboardFocus && hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.textTertiary, 1));
            p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusMd));
        }
    }

   private:
    Motion::StateAnimator* m_hover;
    bool m_clickable = false;
    bool m_pressed = false;
    bool m_keyboardFocus = false;
};

}  // namespace

DownloadsPanel::DownloadsPanel(Downloads::DownloadCenter* center, QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      m_center(center),
      m_shadow(new PopoverShadow(this)),
      m_panel(new PopoverPanel(this)),
      m_scroll(new QScrollArea(m_panel)),
      m_clear(new QPushButton(tr("Clear history"), m_panel)),
      m_open(new QVariantAnimation(this)),
      m_exists([](const QString& id) { return !id.isEmpty(); })
{
    setObjectName(QStringLiteral("zeroDownloadsPanel"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoMouseReplay, true);  // a click on the Downloads button closes the popup instead of reopening it
    setAccessibleName(tr("Downloads"));

    auto* header = new QWidget(m_panel);
    auto* headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(Tokens::spaceLg, Tokens::spaceMd, Tokens::spaceSm, Tokens::spaceXs);
    headerLay->setSpacing(Tokens::spaceSm);
    auto* title = makeLabel(tr("Downloads"), Role::Title, header);
    title->setObjectName(QStringLiteral("zeroDownloadsTitle"));
    headerLay->addWidget(title, 1);
    m_clear->setObjectName(QStringLiteral("zeroDownloadsClear"));
    m_clear->setParent(header);
    Style::setProperty(m_clear, "zeroVariant", "ghost");
    m_clear->setAutoDefault(false);
    m_clear->setCursor(Qt::PointingHandCursor);
    m_clear->hide();
    headerLay->addWidget(m_clear);

    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);

    auto* panelLay = new QVBoxLayout(m_panel);
    panelLay->setContentsMargins(1, 1, 1, 1);
    panelLay->setSpacing(0);
    panelLay->addWidget(header);
    panelLay->addWidget(m_scroll, 1);

    connect(m_clear, &QPushButton::clicked, this, [this]() {
        if (m_center)
            m_center->clearHistory();
    });

    m_open->setStartValue(0.0);
    m_open->setEndValue(1.0);
    connect(m_open, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const qreal p = v.toReal();
        setWindowOpacity(p);
        layoutPanel(qRound((1.0 - p) * kRisePx));
    });

    if (m_center) {
        connect(m_center, &Downloads::DownloadCenter::jobAdded, this, &DownloadsPanel::rebuild);
        connect(m_center, &Downloads::DownloadCenter::jobFinished, this, &DownloadsPanel::rebuild);
        connect(m_center, &Downloads::DownloadCenter::historyChanged, this, &DownloadsPanel::rebuild);
        connect(m_center, &Downloads::DownloadCenter::jobChanged, this, &DownloadsPanel::updateRow);
    }
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        m_signature.clear();  // colors and fonts of the rows follow the theme: build them again
        rebuild();
    });
    rebuild();
}

void DownloadsPanel::setInstanceExists(std::function<bool(const QString&)> exists)
{
    m_exists = exists ? std::move(exists) : [](const QString& id) { return !id.isEmpty(); };
    m_signature.clear();
    rebuild();
}

QString DownloadsPanel::signature() const
{
    QStringList parts;
    if (m_center) {
        for (const auto& a : m_center->activeJobs())
            parts << a.id;
        parts << QStringLiteral("|");
        for (const auto& h : m_center->history())
            parts << h.id + Downloads::stateToString(h.state);
    }
    return parts.join(QLatin1Char(','));
}

void DownloadsPanel::rebuild()
{
    if (!m_center)
        return;
    const QString sig = signature();
    if (m_content && sig == m_signature) {
        for (auto it = m_activeRows.constBegin(); it != m_activeRows.constEnd(); ++it)
            updateRow(it.key());
        return;
    }
    m_signature = sig;

    const auto active = m_center->activeJobs();
    const auto& history = m_center->history();

    m_clear->setVisible(!history.isEmpty());

    // The old rows go away after the current event (a cancel button may be what triggered this).
    if (m_content) {
        QWidget* old = m_scroll->takeWidget();  // QScrollArea::setWidget would delete it on the spot
        old->hide();
        old->deleteLater();
    }
    m_activeRows.clear();
    m_historyRows.clear();

    m_content = new QWidget;
    m_content->setObjectName(QStringLiteral("zeroDownloadsContent"));
    auto* lay = new QVBoxLayout(m_content);
    lay->setContentsMargins(Tokens::spaceSm, 0, Tokens::spaceSm, Tokens::spaceSm);
    lay->setSpacing(0);

    auto section = [&](const QString& text) {
        auto* l = makeLabel(text, Role::Overline, m_content, "tertiary");
        l->setContentsMargins(Tokens::spaceSm, Tokens::spaceMd, Tokens::spaceSm, Tokens::spaceXs);
        lay->addWidget(l);
    };

    if (!active.isEmpty()) {
        section(tr("In progress"));
        for (const ActiveDownload& a : active) {
            auto* row = new QWidget(m_content);
            row->setObjectName(QStringLiteral("zeroDownloadActiveRow"));
            row->setProperty("downloadId", a.id);
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm);
            rl->setSpacing(Tokens::spaceMd);
            rl->addWidget(new KindTile(a.kind, row), 0, Qt::AlignTop);

            auto* mid = new QVBoxLayout;
            mid->setContentsMargins(0, 0, 0, 0);
            mid->setSpacing(Tokens::spaceXs);
            auto* titleLabel = makeElided(a.title, Role::Label, row);
            titleLabel->setObjectName(QStringLiteral("zeroDownloadTitle"));
            auto* caption = makeElided(Downloads::activeCaption(a), Role::Caption, row, "secondary");
            caption->setObjectName(QStringLiteral("zeroDownloadCaption"));
            auto* bar = new DownloadBar(row);
            bar->setFraction(a.fraction);
            mid->addWidget(titleLabel);
            mid->addWidget(caption);
            mid->addSpacing(Tokens::spaceXs);
            mid->addWidget(bar);
            rl->addLayout(mid, 1);

            auto* cancel = new QToolButton(row);
            cancel->setObjectName(QStringLiteral("zeroDownloadCancel"));
            Style::setProperty(cancel, "zeroVariant", "ghost");
            Style::setProperty(cancel, "zeroSize", "icon");
            cancel->setFixedSize(kCancelSize, kCancelSize);
            cancel->setIconSize(QSize(14, 14));
            cancel->setIcon(Icons::icon(QStringLiteral("x"), Theme::current().textSecondary));
            cancel->setToolTip(tr("Cancel download"));
            cancel->setAccessibleName(tr("Cancel download"));
            cancel->setAccessibleDescription(a.title);
            cancel->setCursor(Qt::PointingHandCursor);
            cancel->setAutoRaise(false);
            cancel->setVisible(a.canCancel);
            rl->addWidget(cancel, 0, Qt::AlignTop);
            const QString id = a.id;
            connect(cancel, &QToolButton::clicked, this, [this, id]() {
                if (m_center)
                    m_center->cancel(id);
            });

            lay->addWidget(row);
            m_activeRows.insert(a.id, row);
        }
    }

    if (!history.isEmpty()) {
        section(tr("Recent"));
        const qint64 nowMs = m_center->now();
        for (const HistoryEntry& h : history) {
            auto* row = new HistoryRow(m_content);
            row->setObjectName(QStringLiteral("zeroDownloadHistoryRow"));
            row->setProperty("downloadId", h.id);
            const bool clickable = h.state != DownloadResult::State::Cancelled && m_exists && m_exists(h.instanceId);
            row->setClickable(clickable);
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm);
            rl->setSpacing(Tokens::spaceMd);
            rl->addWidget(new KindTile(h.kind, row), 0, Qt::AlignTop);

            auto* mid = new QVBoxLayout;
            mid->setContentsMargins(0, 0, 0, 0);
            mid->setSpacing(Tokens::spaceXs / 2);
            auto* titleLabel = makeElided(h.title, Role::Label, row);
            titleLabel->setObjectName(QStringLiteral("zeroDownloadTitle"));
            auto* caption = makeElided(Downloads::historyCaption(h, nowMs), Role::Caption, row, "secondary");
            caption->setObjectName(QStringLiteral("zeroDownloadCaption"));
            mid->addWidget(titleLabel);
            mid->addWidget(caption);
            if (h.state == DownloadResult::State::Failed && !h.error.isEmpty()) {
                auto* error = makeElided(h.error, Role::Caption, row, "danger");
                error->setObjectName(QStringLiteral("zeroDownloadError"));
                mid->addWidget(error);
            }
            rl->addLayout(mid, 1);

            const auto tone = h.state == DownloadResult::State::Done     ? PillTag::Tone::Success
                              : h.state == DownloadResult::State::Failed ? PillTag::Tone::Danger
                                                                         : PillTag::Tone::Plain;
            auto* pill = new PillTag(Downloads::stateText(h.state), tone, row);
            pill->setObjectName(QStringLiteral("zeroDownloadPill"));
            rl->addWidget(pill, 0, Qt::AlignTop);

            row->setAccessibleName(tr("%1, %2").arg(h.title, Downloads::stateText(h.state)));
            if (clickable) {
                const QString instanceId = h.instanceId;
                const Kind kind = h.kind;
                row->onClick = [this, instanceId, kind]() {
                    emit openInstanceRequested(instanceId, kind);
                    close();
                };
            }
            lay->addWidget(row);
            m_historyRows.append(row);
        }
    }
    lay->addStretch(1);
    m_scroll->setWidget(m_content);  // takes ownership of the new content (the old one is deleted later)
    m_content->setAutoFillBackground(false);  // the popover surface shows through
    m_content->show();

    if (active.isEmpty() && history.isEmpty()) {
        if (isVisible())
            close();
        return;
    }
    if (isVisible())
        place();
}

void DownloadsPanel::updateRow(const QString& jobId)
{
    QWidget* row = m_activeRows.value(jobId);
    if (!row || !m_center)
        return;
    const auto job = m_center->activeJob(jobId);
    if (!job)
        return;
    if (auto* caption = row->findChild<ElidedLabel*>(QStringLiteral("zeroDownloadCaption"))) {
        const QString text = Downloads::activeCaption(*job);
        if (text != caption->fullText())
            caption->setFullText(text);
    }
    if (auto* bar = static_cast<DownloadBar*>(row->findChild<QWidget*>(QStringLiteral("zeroDownloadBar"))))
        bar->setFraction(job->fraction);
    if (auto* cancel = row->findChild<QToolButton*>(QStringLiteral("zeroDownloadCancel")))
        cancel->setVisible(job->canCancel);
}

int DownloadsPanel::contentHeight() const
{
    if (!m_content)
        return 0;
    m_content->ensurePolished();
    return m_content->layout()->sizeHint().height();
}

void DownloadsPanel::layoutPanel(int riseOffset)
{
    const QRect panel(kMargin, kMargin - Tokens::shadowOffsetY + riseOffset, width() - 2 * kMargin, height() - 2 * kMargin);
    m_panel->setGeometry(panel);
    m_shadow->setGeometry(panel);
    m_shadow->lower();
}

void DownloadsPanel::place()
{
    // Height: header + rows, at most kMaxHeight (and what the screen has).
    QWidget* header = m_panel->layout()->itemAt(0)->widget();
    header->ensurePolished();
    int panelH = header->sizeHint().height() + contentHeight() + 2;
    panelH = std::min(panelH, kMaxHeight);

    QPoint topLeft = pos() + QPoint(kMargin, kMargin - Tokens::shadowOffsetY);
    QRect avail(0, 0, 1920, 1080);
    if (m_anchor) {
        const QPoint anchorBottomRight = m_anchor->mapToGlobal(QPoint(m_anchor->width(), m_anchor->height()));
        topLeft = QPoint(anchorBottomRight.x() - kWidth, anchorBottomRight.y() + Tokens::spaceXs);
        if (QScreen* screen = m_anchor->screen())
            avail = screen->availableGeometry();
        topLeft.setX(std::clamp(topLeft.x(), avail.left() + Tokens::spaceSm, std::max(avail.left() + Tokens::spaceSm, avail.right() - kWidth - Tokens::spaceSm)));
        panelH = std::min(panelH, std::max(120, avail.bottom() - Tokens::spaceSm - topLeft.y()));
    }
    setGeometry(topLeft.x() - kMargin, topLeft.y() - kMargin + Tokens::shadowOffsetY, kWidth + 2 * kMargin, panelH + 2 * kMargin);
    layoutPanel(0);
}

void DownloadsPanel::toggle(QWidget* anchor)
{
    if (isVisible())
        close();
    else
        popup(anchor);
}

void DownloadsPanel::popup(QWidget* anchor)
{
    if (!anchor || !m_center || (m_center->activeCount() == 0 && m_center->history().isEmpty()))
        return;
    m_anchor = anchor;
    m_signature.clear();  // the "2 min ago" captions are as old as the last build
    rebuild();
    place();
    m_open->stop();
    const int ms = Motion::duration(kOpenMs);
    if (ms > 0) {
        setWindowOpacity(0.0);
        layoutPanel(kRisePx);
    } else {
        setWindowOpacity(1.0);
    }
    show();
    raise();
    activateWindow();
    if (ms > 0) {
        m_open->setDuration(ms);
        m_open->setEasingCurve(Motion::easeEnter());
        m_open->start();
    }
    emit opened();
}

void DownloadsPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_anchor)
        place();  // shown without popup(): size it where it is
}

void DownloadsPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_open->stop();
    setWindowOpacity(1.0);
    if (m_anchor && m_anchor->isVisible())
        m_anchor->setFocus(Qt::PopupFocusReason);
    emit closed();
}

void DownloadsPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

}  // namespace Zero
