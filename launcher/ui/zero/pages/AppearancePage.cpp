// SPDX-License-Identifier: GPL-3.0-only
#include "AppearancePage.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include "Application.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"
#include "ui/themes/ITheme.h"
#include "ui/themes/ThemeManager.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroPixelFont.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroTheme.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTransition.h"

namespace Zero {

namespace {

constexpr int kContentMaxWidth = 960;
constexpr int kPreviewMaxHeight = 340;
constexpr int kStagePadding = 12;
constexpr qreal kLogicalW = 640.0;  // the miniature window is drawn in a 640x360 space and scaled
constexpr qreal kLogicalH = 360.0;

QLabel* makeLabel(const QString& text, Role role, QWidget* parent, bool secondary = false)
{
    auto* l = new QLabel(text, parent);
    Style::applyRole(l, role);
    if (secondary)
        Style::setProperty(l, "zeroTone", "secondary");
    return l;
}

/// Font for a type role of `spec` (the preview must not use the active theme's fonts).
QFont specFont(const ThemeSpec& spec, Role role, int px, int weight)
{
    QFont f;
    if (spec.usesPixelFont(role) && registerPixelFont()) {
        f.setFamilies({ pixelFamily() });
        f.setPixelSize(qMax(10, int(qRound(px / 10.0)) * 10));
        f.setWeight(QFont::Normal);
        f.setHintingPreference(QFont::PreferNoHinting);
        f.setKerning(false);
        f.setStyleStrategy(QFont::NoAntialias);
        return f;
    }
    f.setFamilies({ QLatin1String(Tokens::fontFamilyPrimary), QLatin1String(Tokens::fontFamilyFallback) });
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

/// Raised-block edges for `spec` (the bevel of its buttons): 1 px light top, 2 px dark bottom, inside `r`.
void paintSpecBevel(QPainter& p, const QRectF& r, const ThemeSpec& spec)
{
    if (!spec.bevel)
        return;
    p.save();
    p.setClipPath(Paint::panelPath(r, spec.radiusMd, spec.corners));
    p.fillRect(QRectF(r.left(), r.top(), r.width(), 1.0), withAlpha(Qt::white, Tokens::bevelLightAlpha * 2.0));
    p.fillRect(QRectF(r.left(), r.bottom() - 2.0, r.width(), 2.0), withAlpha(spec.bevelDark, 0.6));
    p.restore();
}

void fillTextured(QPainter& p, const QRectF& r, const QColor& base, const ThemeSpec& spec)
{
    if (spec.texture)
        p.drawTiledPixmap(r.toAlignedRect(), Style::texture(spec, base));
    else
        p.fillRect(r, base);
}

/// Paints a miniature ZERO window for `spec` inside `window` (any size, 16:9). Drawn in a 640x360 logical space.
void paintMiniWindow(QPainter& p, const QRectF& window, const ThemeSpec& spec)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(window.topLeft());
    const qreal k = window.width() / kLogicalW;
    p.scale(k, k);

    const QRectF win(0.5, 0.5, kLogicalW - 1.0, kLogicalH - 1.0);
    const QPainterPath winPath = Paint::panelPath(win, spec.radiusLg, spec.corners);
    p.setClipPath(winPath);
    fillTextured(p, win, spec.bg, spec);

    // rail
    const QRectF rail(0, 0, 140, kLogicalH);
    fillTextured(p, rail, spec.surface, spec);
    p.fillRect(QRectF(rail.right() - 1, 0, 1, kLogicalH), spec.border);
    p.setPen(spec.text);
    p.setFont(specFont(spec, Role::Title, 20, 600));
    p.drawText(QRectF(16, 14, 110, 26), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("ZERO"));
    for (int i = 0; i < 3; ++i) {
        const QRectF item(10, 62 + i * 34, rail.width() - 20, 28);
        if (i == 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(spec.elevated);
            p.drawPath(Paint::panelPath(item, spec.radiusMd, spec.corners));
            p.setBrush(spec.accent);
            p.drawRect(QRectF(item.left() - 10, item.top() + 5, 2, item.height() - 10));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(i == 0 ? spec.text : spec.textTertiary);
        p.drawRoundedRect(QRectF(item.left() + 10, item.center().y() - 5, 10, 10), 2, 2);
        p.setBrush(i == 0 ? spec.text : spec.textSecondary);
        p.drawRoundedRect(QRectF(item.left() + 28, item.center().y() - 2.5, 40 + (i == 1 ? 8 : 0), 5), 2.5, 2.5);
    }

    // page header
    p.setPen(spec.text);
    p.setFont(specFont(spec, Role::Title, 20, 600));
    p.drawText(QRectF(rail.right() + 24, 14, 200, 28), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Home"));

    // hero card
    const QRectF hero(rail.right() + 24, 56, kLogicalW - rail.right() - 48, 150);
    p.setBrush(spec.surface);
    p.setPen(QPen(spec.border, 1));
    p.drawPath(Paint::panelPath(hero.adjusted(0.5, 0.5, -0.5, -0.5), spec.radiusLg, spec.corners));
    const QRectF tile(hero.left() + 18, hero.top() + 18, 114, 114);
    p.setPen(Qt::NoPen);
    p.setBrush(spec.elevated);
    p.drawPath(Paint::panelPath(tile, spec.radiusLg, spec.corners));
    p.setBrush(spec.accent2);
    p.drawPath(Paint::panelPath(QRectF(tile.center().x() - 24, tile.center().y() - 24, 48, 48), spec.radiusMd, spec.corners));
    const qreal tx = tile.right() + 22;
    p.setBrush(spec.textTertiary);
    p.drawRoundedRect(QRectF(tx, hero.top() + 22, 60, 6), 3, 3);
    p.setBrush(spec.text);
    p.drawRoundedRect(QRectF(tx, hero.top() + 40, 170, 12), 3, 3);
    p.setBrush(spec.textSecondary);
    p.drawRoundedRect(QRectF(tx, hero.top() + 62, 120, 6), 3, 3);
    const QRectF btn(tx, hero.bottom() - 52, 112, 34);
    p.setBrush(spec.accent);
    p.drawPath(Paint::panelPath(btn, spec.radiusMd, spec.corners));
    paintSpecBevel(p, btn, spec);
    p.setPen(spec.onAccent);
    p.setFont(specFont(spec, Role::Label, 14, 600));
    p.drawText(btn, Qt::AlignCenter, QStringLiteral("Play"));
    p.setBrush(spec.elevated);
    p.setPen(QPen(spec.border, 1));
    p.drawPath(Paint::panelPath(QRectF(btn.right() + 10, btn.top(), 64, 34), spec.radiusMd, spec.corners));

    // two small cards
    const qreal cw = (hero.width() - 16) / 2.0;
    for (int i = 0; i < 2; ++i) {
        const QRectF card(hero.left() + i * (cw + 16), hero.bottom() + 16, cw, 64);
        p.setBrush(spec.surface);
        p.setPen(QPen(spec.border, 1));
        p.drawPath(Paint::panelPath(card.adjusted(0.5, 0.5, -0.5, -0.5), spec.radiusLg, spec.corners));
        p.setPen(Qt::NoPen);
        p.setBrush(spec.elevated);
        p.drawPath(Paint::panelPath(QRectF(card.left() + 12, card.top() + 14, 36, 36), spec.radiusMd, spec.corners));
        p.setBrush(spec.text);
        p.drawRoundedRect(QRectF(card.left() + 60, card.top() + 20, 80, 8), 4, 4);
        p.setBrush(spec.textTertiary);
        p.drawRoundedRect(QRectF(card.left() + 60, card.top() + 38, 54, 6), 3, 3);
    }

    p.setClipping(false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(spec.borderStrong, 1));
    p.drawPath(winPath);
    p.restore();
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------

/// Miniature ZERO window painted from a ThemeSpec; crossfades when the spec changes.
class ThemePreview : public QWidget {
   public:
    explicit ThemePreview(QWidget* parent) : QWidget(parent), m_anim(this)
    {
        m_from = m_to = Theme::current();
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_anim.setStartValue(0.0);
        m_anim.setEndValue(1.0);
        m_anim.setEasingCurve(Motion::easeEnter());
        connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_t = v.toReal();
            update();
        });
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

    void setSpec(const ThemeSpec& spec, bool animate)
    {
        if (spec.id == m_to.id && m_t >= 1.0)
            return;
        m_anim.stop();
        const int ms = animate ? Motion::duration(Tokens::durBase) : 0;
        if (ms <= 0) {
            m_from = m_to = spec;
            m_t = 1.0;
            update();
            return;
        }
        // Start from what is on screen (a half-faded preview keeps its blend as the new "from" only approximately:
        // the target of the interrupted fade wins when it is already mostly visible).
        if (m_t >= 0.5)
            m_from = m_to;
        m_to = spec;
        m_t = 0.0;
        m_anim.setDuration(ms);
        m_anim.start();
    }

    const ThemeSpec& spec() const { return m_to; }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return qMin(kPreviewMaxHeight, w * 9 / 16); }
    QSize sizeHint() const override { return { 640, heightForWidth(640) }; }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF stage = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(t.surface);
        p.setPen(QPen(t.border, 1));
        p.drawPath(Paint::panelPath(stage, t.radiusLg));

        const QRectF avail = stage.adjusted(kStagePadding, kStagePadding, -kStagePadding, -kStagePadding);
        const qreal k = qMin(avail.width() / kLogicalW, avail.height() / kLogicalH);
        const QRectF window(avail.center().x() - kLogicalW * k / 2.0, avail.center().y() - kLogicalH * k / 2.0, kLogicalW * k, kLogicalH * k);
        paintMiniWindow(p, window, m_from);
        if (m_t > 0.0) {
            p.setOpacity(m_t);
            paintMiniWindow(p, window, m_to);
        }
    }

   private:
    ThemeSpec m_from, m_to;
    qreal m_t = 1.0;
    QVariantAnimation m_anim;
};

/// Four swatches of a ThemeSpec: bg, surface, accent, accent2.
class SwatchRow : public QWidget {
   public:
    SwatchRow(const ThemeSpec& spec, QWidget* parent) : QWidget(parent), m_spec(spec)
    {
        setFixedHeight(24);
        setMinimumWidth(4 * 32 + 3 * 6);
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor colors[4] = { m_spec.bg, m_spec.surface, m_spec.accent, m_spec.accent2 };
        int x = 0;
        for (const QColor& c : colors) {
            const QRectF r(x + 0.5, 0.5, 31, 23);
            p.setBrush(c);
            p.setPen(QPen(m_spec.borderStrong, 1));
            p.drawPath(Paint::panelPath(r, m_spec.radiusSm, m_spec.corners));
            x += 38;
        }
    }

   private:
    ThemeSpec m_spec;
};

/// Selectable card for one theme: swatches, name, tooltip text and a "Current" badge.
class ThemeCard : public ZeroCard {
   public:
    ThemeCard(const QString& id, const ThemeSpec& spec, const QString& name, const QString& description, QWidget* parent)
        : ZeroCard(parent), m_id(id), m_spec(spec)
    {
        setClickable(true);
        setMinimumHeight(112);
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
        lay->setSpacing(Tokens::spaceSm);
        auto* top = new QHBoxLayout;
        top->setContentsMargins(0, 0, 0, 0);
        top->addWidget(new SwatchRow(spec, this), 0, Qt::AlignLeft);
        top->addStretch(1);
        m_badge = new QLabel(tr("Current"), this);
        Style::setProperty(m_badge, "zeroBadge", "neutral");
        m_badge->hide();
        top->addWidget(m_badge);
        lay->addLayout(top);
        lay->addWidget(makeLabel(name, Role::Heading, this));
        auto* desc = makeLabel(description, Role::Caption, this, true);
        desc->setWordWrap(true);
        lay->addWidget(desc);
        lay->addStretch(1);
        setToolTip(description);
        setAccessibleName(name);
    }

    QString id() const { return m_id; }
    const ThemeSpec& spec() const { return m_spec; }
    void setCurrent(bool current) { m_badge->setVisible(current); }
    void setSelected(bool selected)
    {
        m_selected = selected;
        update();
    }

   protected:
    void paintEvent(QPaintEvent* e) override
    {
        ZeroCard::paintEvent(e);
        if (!m_selected)
            return;
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.accent2, 2));
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusLg));
    }

   private:
    QString m_id;
    ThemeSpec m_spec;
    QLabel* m_badge;
    bool m_selected = false;
};

// ---------------------------------------------------------------------------------------------------------------------

AppearancePage::AppearancePage(QWidget* parent) : ShellPage(parent)
{
    auto* pageLay = new QVBoxLayout(this);
    pageLay->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inner = new QWidget;
    scroll->setWidget(inner);
    inner->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    pageLay->addWidget(scroll);

    auto* outerRow = new QHBoxLayout(inner);
    outerRow->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
    outerRow->addStretch(1);
    auto* column = new QWidget(inner);
    column->setMaximumWidth(kContentMaxWidth);
    outerRow->addWidget(column, 1000);
    outerRow->addStretch(1);

    auto* col = new QVBoxLayout(column);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // ----- Theme -----
    col->addWidget(makeLabel(tr("Theme"), Role::Heading, column));
    col->addSpacing(Tokens::spaceMd);
    m_preview = new ThemePreview(column);
    col->addWidget(m_preview);
    col->addSpacing(Tokens::spaceLg);
    m_cardRow = new QWidget(column);
    auto* cards = new QHBoxLayout(m_cardRow);
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(Tokens::spaceLg);
    col->addWidget(m_cardRow);
    col->addSpacing(Tokens::spaceLg);

    auto* applyRow = new QHBoxLayout;
    applyRow->setContentsMargins(0, 0, 0, 0);
    applyRow->addStretch(1);
    m_apply = new QPushButton(tr("Apply theme"), column);
    Style::setProperty(m_apply, "zeroVariant", "primary");
    connect(m_apply, &QPushButton::clicked, this, [this]() {
        if (!m_selectedId.isEmpty() && m_selectedId != m_currentId)
            emit applyThemeRequested(m_selectedId);
    });
    applyRow->addWidget(m_apply);
    col->addLayout(applyRow);
    col->addSpacing(Tokens::spaceXxl);

    // ----- Motion -----
    col->addWidget(makeLabel(tr("Motion"), Role::Heading, column));
    col->addSpacing(Tokens::spaceMd);
    auto* motionRow = new QHBoxLayout;
    motionRow->setContentsMargins(0, 0, 0, 0);
    motionRow->setSpacing(Tokens::spaceXs);
    m_motionGroup = new QButtonGroup(this);
    m_motionGroup->setExclusive(true);
    const struct {
        const char* value;
        QString label;
    } options[] = { { "system", tr("Follow Windows") }, { "on", tr("On") }, { "off", tr("Off") } };
    for (const auto& o : options) {
        auto* b = new QPushButton(o.label, column);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        Style::setProperty(b, "zeroVariant", "ghost");
        b->setProperty("zeroMotionValue", QString::fromLatin1(o.value));
        m_motionGroup->addButton(b);
        motionRow->addWidget(b);
    }
    motionRow->addSpacing(Tokens::spaceMd);
    auto* previewBtn = new QPushButton(tr("Preview"), column);
    Style::setProperty(previewBtn, "zeroVariant", "ghost");
    previewBtn->setToolTip(tr("Replays a transition on the theme preview"));
    connect(previewBtn, &QPushButton::clicked, this,
            [this]() { Transition::run(m_preview, Transition::Kind::Rise, 0, [this]() { m_preview->update(); }); });
    motionRow->addWidget(previewBtn);
    motionRow->addStretch(1);
    col->addLayout(motionRow);
    col->addSpacing(Tokens::spaceSm);
    m_motionCaption = makeLabel({}, Role::Caption, column, true);
    m_motionCaption->setWordWrap(true);
    col->addWidget(m_motionCaption);
    col->addSpacing(Tokens::spaceXxl);

    connect(m_motionGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton* b) {
        APPLICATION->settings()->set("ZeroMotion", b->property("zeroMotionValue").toString());
        syncMotion();
    });

    auto* more = new QPushButton(tr("More appearance options"), column);
    Style::setProperty(more, "zeroVariant", "ghost");
    more->setCursor(Qt::PointingHandCursor);
    connect(more, &QPushButton::clicked, this, &AppearancePage::moreOptionsRequested);
    auto* moreRow = new QHBoxLayout;
    moreRow->setContentsMargins(0, 0, 0, 0);
    moreRow->addWidget(more);
    moreRow->addStretch(1);
    col->addLayout(moreRow);
    col->addStretch(1);

    buildThemeCards();
    syncMotion();

    // Keep in sync with the theme changing elsewhere (Prism's settings dialog, the legacy menu) and with the setting.
    connect(&Theme::instance(), &Theme::changed, this, [this]() { QTimer::singleShot(0, this, &AppearancePage::syncCurrent); });
    if (auto setting = APPLICATION->settings()->getSetting("ApplicationTheme"))
        connect(setting.get(), &Setting::SettingChanged, this, [this](const Setting&, const QVariant&) { syncCurrent(); });
    for (const char* key : { "ZeroMotion", "ZeroReduceMotion" }) {
        if (auto setting = APPLICATION->settings()->getSetting(key))
            connect(setting.get(), &Setting::SettingChanged, this, [this](const Setting&, const QVariant&) { syncMotion(); });
    }
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        if (s == Qt::ApplicationActive)
            syncMotion();  // the user may have flipped Windows "Animation effects"
    });
}

QString AppearancePage::currentThemeId() const
{
    const QString id = APPLICATION->settings()->get("ApplicationTheme").toString();
    return id.isEmpty() ? QString::fromLatin1(kDefaultThemeId) : id;  // empty = ThemeManager's ZERO default
}

void AppearancePage::buildThemeCards()
{
    auto* lay = qobject_cast<QHBoxLayout*>(m_cardRow->layout());
    m_currentId = currentThemeId();
    for (ITheme* theme : APPLICATION->themeManager()->getValidApplicationThemes()) {
        ThemeSpec spec;
        if (!isZeroThemeId(theme->id()) || !specForThemeId(theme->id(), &spec))
            continue;
        auto* card = new ThemeCard(theme->id(), spec, theme->name(), theme->tooltip(), m_cardRow);
        connect(card, &ZeroCard::clicked, this, [this, id = theme->id()]() { selectTheme(id); });
        lay->addWidget(card, 1);
        m_cards << card;
    }
    lay->addStretch(m_cards.size() < 3 ? 3 - m_cards.size() : 0);
    // Start on the applied theme (or the first card when a non-ZERO theme is active).
    QString start = m_currentId;
    bool found = false;
    for (auto* c : std::as_const(m_cards))
        found = found || c->id() == start;
    if (!found && !m_cards.isEmpty())
        start = m_cards.first()->id();
    m_selectedId.clear();
    selectTheme(start);
    for (auto* c : std::as_const(m_cards))
        c->setCurrent(c->id() == m_currentId);
    refreshApplyButton();
}

void AppearancePage::selectTheme(const QString& id)
{
    if (id == m_selectedId)
        return;
    for (auto* c : std::as_const(m_cards)) {
        c->setSelected(c->id() == id);
        if (c->id() == id) {
            m_preview->setSpec(c->spec(), !m_selectedId.isEmpty());
            m_selectedId = id;
        }
    }
    refreshApplyButton();
}

void AppearancePage::syncCurrent()
{
    const QString now = currentThemeId();
    if (now == m_currentId)
        return;
    m_currentId = now;
    for (auto* c : std::as_const(m_cards))
        c->setCurrent(c->id() == m_currentId);
    refreshApplyButton();
}

void AppearancePage::refreshApplyButton()
{
    m_apply->setEnabled(!m_selectedId.isEmpty() && m_selectedId != m_currentId);
}

void AppearancePage::syncMotion()
{
    auto* settings = APPLICATION->settings();
    QString value = settings->get("ZeroMotion").toString();
    if (value != QLatin1String("on") && value != QLatin1String("off"))
        value = (value.isEmpty() && settings->get("ZeroReduceMotion").toBool()) ? QStringLiteral("off") : QStringLiteral("system");
    for (auto* b : m_motionGroup->buttons())
        b->setChecked(b->property("zeroMotionValue").toString() == value);

    if (value == QLatin1String("on")) {
        m_motionCaption->setText(tr("Animations are always on."));
    } else if (value == QLatin1String("off")) {
        m_motionCaption->setText(tr("Everything changes instantly. Activity indicators, like the launch sweep, still run."));
    } else if (Motion::systemAnimationsEnabled()) {
        m_motionCaption->setText(tr("Windows animation effects are on, so ZERO animates."));
    } else {
        m_motionCaption->setText(tr("Windows animation effects are off on this PC, so ZERO stays still. Choose On to enable animations."));
    }
}

}  // namespace Zero
