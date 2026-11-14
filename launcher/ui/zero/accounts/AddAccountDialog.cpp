// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Add account" dialog (see AddAccountDialog.h).
#include "AddAccountDialog.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QtMath>
#include <algorithm>
#include <memory>

#include "Application.h"
#include "DesktopServices.h"
#include "minecraft/auth/AccountList.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

#include "qrencode.h"

namespace Zero {
namespace Accounts {

namespace {

constexpr int kContentWidth = AddAccountDialog::kWidth - 2 * Tokens::spaceXl;
constexpr int kCardHeight = 64;
constexpr int kTile = 36;
constexpr int kQrSize = 104;
constexpr int kGlyphWidth = 24;
constexpr int kGlyphHeight = 16;

QString dtr(const char* text)
{
    return QCoreApplication::translate("Zero::Accounts::AddAccountDialog", text);
}

QLabel* makeLabel(const QString& text, Role role, QWidget* parent, const char* tone = nullptr, bool wrap = true)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    if (tone)
        Style::setProperty(label, "zeroTone", QString::fromLatin1(tone));
    label->setWordWrap(wrap);
    return label;
}

/// The lighter and the darker of two theme colors, so the QR code keeps its scanning contrast in light custom themes too.
QColor qrLight(const ThemeSpec& t)
{
    return t.text.lightnessF() >= t.bg.lightnessF() ? t.text : t.bg;
}
QColor qrDark(const ThemeSpec& t)
{
    return t.text.lightnessF() >= t.bg.lightnessF() ? t.bg : t.text;
}

/// "microsoft.com/link" for "https://www.microsoft.com/link?otc=ABC": host without "www." plus the path, without the query.
QString displayAddress(const QUrl& url)
{
    if (!url.isValid() || url.host().isEmpty())
        return {};
    QString host = url.host();
    if (host.startsWith(QLatin1String("www.")))
        host.remove(0, 4);
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return host + path;
}

/// First non-empty line of a flow's message, bounded: the flow's reason can carry several lines of detail.
QString firstLine(const QString& reason)
{
    for (const QString& line : reason.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            return trimmed.size() > 240 ? trimmed.left(239) + QChar(0x2026) : trimmed;
    }
    return {};
}

/// Height of a page laid out at `width` (the page's layout has no outer margins).
int heightForPage(QWidget* page, int width)
{
    QLayout* layout = page->layout();
    if (!layout)
        return page->sizeHint().height();
    const int h = layout->hasHeightForWidth() ? layout->totalHeightForWidth(width) : layout->totalSizeHint().height();
    return qMax(h, layout->totalMinimumSize().height());
}

QFont codeFont()
{
    if (Theme::current().usesPixelFont(Role::Title))
        return Style::font(Role::Title);  // ZERO Pixel at 20 px (Minecraft)
    QFont f;
    f.setFamilies({ QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("Courier New") });
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setPixelSize(20);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    return f;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// OptionCard: one kind of account (icon tile, title, caption, chevron)
// ---------------------------------------------------------------------------------------------------------------------

class OptionCard : public ZeroCard {
   public:
    OptionCard(AccountType type, const QString& icon, const QString& title, const QString& caption, QWidget* parent)
        : ZeroCard(parent), m_type(type), m_icon(icon)
    {
        setClickable(true);
        setFocusRingVisible(false);  // the ring is painted below, in accent2, and only for keyboard focus
        setContentsMargins(0, 0, 0, 0);
        setMinimumHeight(kCardHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName(title);
        setAccessibleDescription(caption);

        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(Tokens::spaceLg, Tokens::spaceMd, Tokens::spaceLg, Tokens::spaceMd);
        row->setSpacing(Tokens::spaceMd);
        m_tile = new QLabel(this);
        m_tile->setProperty("zeroTile", true);
        m_tile->setFixedSize(kTile, kTile);
        m_tile->setAlignment(Qt::AlignCenter);
        m_tile->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(m_tile, 0, Qt::AlignVCenter);
        auto* column = new QVBoxLayout;
        column->setSpacing(2);
        m_title = makeLabel(title, Role::Heading, this, nullptr, false);
        m_caption = makeLabel(caption, Role::Caption, this, "secondary");
        m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_caption->setAttribute(Qt::WA_TransparentForMouseEvents);
        column->addWidget(m_title);
        column->addWidget(m_caption);
        row->addLayout(column, 1);
        m_chevron = new QLabel(this);
        m_chevron->setFixedSize(16, 16);
        m_chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(m_chevron, 0, Qt::AlignVCenter);
        refreshIcons();
    }

    AccountType type() const { return m_type; }
    QString title() const { return m_title->text(); }
    QString captionText() const { return m_caption->text(); }
    bool isFocusVisible() const { return m_focusVisible; }

    void refreshIcons()
    {
        const auto& t = Theme::current();
        m_tile->setPixmap(Icons::icon(m_icon, t.accent).pixmap(QSize(20, 20)));
        m_chevron->setPixmap(Icons::icon(QStringLiteral("chevron-right"), t.textTertiary).pixmap(QSize(16, 16)));
    }

   protected:
    void focusInEvent(QFocusEvent* event) override
    {
        // Keyboard focus (Tab, arrows, the dialog's first focus) shows the ring; a click does not.
        const auto reason = event->reason();
        m_focusVisible = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason || reason == Qt::ShortcutFocusReason;
        ZeroCard::focusInEvent(event);
    }
    void focusOutEvent(QFocusEvent* event) override
    {
        m_focusVisible = false;
        ZeroCard::focusOutEvent(event);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        m_focusVisible = false;
        ZeroCard::mousePressEvent(event);
    }
    void paintEvent(QPaintEvent* event) override
    {
        ZeroCard::paintEvent(event);
        if (!m_focusVisible || !hasFocus())
            return;
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.accent2, 2));
        p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusLg));
    }

   private:
    AccountType m_type;
    QString m_icon;
    bool m_focusVisible = false;
    QLabel* m_tile = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_caption = nullptr;
    QLabel* m_chevron = nullptr;
};

// ---------------------------------------------------------------------------------------------------------------------
// Small widgets
// ---------------------------------------------------------------------------------------------------------------------

/// "Copy code": icon + text in accent2, underlined on hover, a ring on keyboard focus.
class LinkButton : public QAbstractButton {
   public:
    LinkButton(const QString& text, const QString& icon, QWidget* parent) : QAbstractButton(parent), m_icon(icon)
    {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover, true);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }
    void setIconName(const QString& icon)
    {
        m_icon = icon;
        update();
    }
    QSize sizeHint() const override
    {
        const QFontMetrics fm(Style::font(Role::Label));
        return { 14 + Tokens::spaceSm + fm.horizontalAdvance(text()) + 2 * Tokens::spaceXs, qMax(fm.height(), 14) + Tokens::spaceSm };
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QFont f = Style::font(Role::Label);
        f.setUnderline(underMouse());
        p.setFont(f);
        const QColor color = isEnabled() ? t.accent2 : t.textTertiary;
        const int cy = height() / 2;
        const int x = Tokens::spaceXs;
        Icons::icon(m_icon, color).paint(&p, QRect(x, cy - 7, 14, 14));
        p.setPen(color);
        p.drawText(QRect(x + 14 + Tokens::spaceSm, 0, width(), height()), Qt::AlignLeft | Qt::AlignVCenter, text());
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.accent2, 1));
            p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusSm, t.radiusSm);
        }
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            click();
            event->accept();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }

   private:
    QString m_icon;
};

/// The QR code of the sign-in link: the lighter theme color as the square, the darker one for the modules.
class QrWidget : public QWidget {
   public:
    explicit QrWidget(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(kQrSize, kQrSize);
        setAccessibleName(dtr("QR code for the sign-in page"));
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }
    void setText(const QString& text)
    {
        m_text = text;
        update();
    }
    QString text() const { return m_text; }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setPen(Qt::NoPen);
        const QPainterPath shape = Paint::panelPath(QRectF(rect()), t.radiusMd);
        const bool on = isEnabled();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(on && !m_text.isEmpty() ? qrLight(t) : t.elevated);
        p.drawPath(shape);
        if (m_text.isEmpty())
            return;
        std::unique_ptr<QRcode, decltype(&QRcode_free)> qr(
            QRcode_encodeString(m_text.toUtf8().constData(), 0, QR_ECLEVEL_M, QR_MODE_8, 1), &QRcode_free);
        if (!qr)
            return;
        constexpr int quiet = 2;  // modules of light around the code
        const int modules = qr->width + 2 * quiet;
        const int scale = qMax(1, qMin(width(), height()) / modules);
        const int size = modules * scale;
        const int ox = (width() - size) / 2 + quiet * scale;
        const int oy = (height() - size) / 2 + quiet * scale;
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setBrush(on ? qrDark(t) : t.textTertiary);
        for (int y = 0; y < qr->width; ++y)
            for (int x = 0; x < qr->width; ++x)
                if (qr->data[y * qr->width + x] & 1)
                    p.drawRect(ox + x * scale, oy + y * scale, scale, scale);
    }

   private:
    QString m_text;
};

/// The glyph before the status text: three pulsing squares while waiting, a check (success) or a cross (error) otherwise.
class StatusGlyph : public QWidget {
   public:
    enum class Mode { Dots, Check, Error };

    explicit StatusGlyph(QWidget* parent) : QWidget(parent), m_anim(this)
    {
        setFixedSize(kGlyphWidth, kGlyphHeight);
        m_anim.setStartValue(0.0);
        m_anim.setEndValue(1.0);
        m_anim.setDuration(1200);
        m_anim.setEasingCurve(QEasingCurve::Linear);
        m_anim.setLoopCount(-1);
        connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_phase = v.toReal();
            update();
        });
        connect(&Theme::instance(), &Theme::changed, this, [this]() {
            sync();
            update();
        });
    }
    void setMode(Mode mode)
    {
        m_mode = mode;
        sync();
        update();
    }
    Mode mode() const { return m_mode; }
    bool isAnimating() const { return m_anim.state() == QAbstractAnimation::Running; }

   protected:
    void showEvent(QShowEvent* e) override
    {
        QWidget::showEvent(e);
        sync();
    }
    void hideEvent(QHideEvent* e) override
    {
        QWidget::hideEvent(e);
        m_anim.stop();
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        if (m_mode == Mode::Check || m_mode == Mode::Error) {
            const bool ok = m_mode == Mode::Check;
            Icons::icon(ok ? QStringLiteral("check") : QStringLiteral("circle-x"), ok ? t.success : t.danger)
                .paint(&p, QRect((width() - 16) / 2, 0, 16, 16));
            return;
        }
        // Squares of 5 px, 3 px apart (the pixel look of both themes); the pulse runs along them.
        constexpr int side = 5;
        constexpr int gap = 3;
        const int total = 3 * side + 2 * gap;
        const int x0 = (width() - total) / 2;
        const int y0 = (height() - side) / 2;
        for (int i = 0; i < 3; ++i) {
            qreal alpha = 1.0;
            if (isAnimating())
                alpha = 0.3 + 0.7 * (0.5 + 0.5 * qCos(2.0 * M_PI * (m_phase - i * 0.16)));
            p.fillRect(QRect(x0 + i * (side + gap), y0, side, side), withAlpha(t.accent, alpha));
        }
    }

   private:
    void sync()
    {
        const bool run = isVisible() && m_mode == Mode::Dots && Motion::enabled();
        if (run && m_anim.state() != QAbstractAnimation::Running)
            m_anim.start();
        else if (!run && m_anim.state() == QAbstractAnimation::Running)
            m_anim.stop();
    }

    QVariantAnimation m_anim;
    qreal m_phase = 0.0;
    Mode m_mode = Mode::Dots;
};

/// The panel of the code: QR code on the left; the code and "Copy code" on the right.
class CodePanel : public ZeroCard {
   public:
    explicit CodePanel(QWidget* parent) : ZeroCard(parent)
    {
        setHoverEnabled(false);
        setContentsMargins(0, 0, 0, 0);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
        row->setSpacing(Tokens::spaceLg);
        m_qr = new QrWidget(this);
        row->addWidget(m_qr, 0, Qt::AlignVCenter);
        auto* column = new QVBoxLayout;
        column->setSpacing(Tokens::spaceSm);
        column->addStretch(1);
        m_code = new QLabel(this);
        m_code->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_code->setAccessibleName(dtr("Sign-in code"));
        column->addWidget(m_code);
        m_copy = new LinkButton(dtr("Copy code"), QStringLiteral("copy"), this);
        m_copy->setAccessibleName(dtr("Copy code"));
        column->addWidget(m_copy, 0, Qt::AlignLeft);
        column->addStretch(1);
        row->addLayout(column, 1);
        refreshFont();
        showPlaceholder();
        connect(&Theme::instance(), &Theme::changed, this, [this]() { refreshFont(); });
    }

    void setCode(const QString& code, const QString& qrText)
    {
        m_code->setText(code);
        m_qr->setText(qrText);
        m_copy->setEnabled(true);
    }
    void clear()
    {
        showPlaceholder();
        m_qr->setText(QString());
        setCopied(false);
    }
    void setCopied(bool copied)
    {
        m_copy->setText(copied ? dtr("Copied") : dtr("Copy code"));
        m_copy->setIconName(copied ? QStringLiteral("check") : QStringLiteral("copy"));
        m_copy->updateGeometry();
        m_copy->update();
    }
    QLabel* codeLabel() const { return m_code; }
    LinkButton* copyButton() const { return m_copy; }
    QString qrText() const { return m_qr->text(); }
    bool hasCode() const { return !m_qr->text().isEmpty(); }

   private:
    void showPlaceholder()
    {
        m_code->setText(QString::fromUtf8("\xE2\x80\xA6"));  // ...
        m_copy->setEnabled(false);
    }
    void refreshFont() { m_code->setFont(codeFont()); }

    QrWidget* m_qr = nullptr;
    QLabel* m_code = nullptr;
    LinkButton* m_copy = nullptr;
};

/// Holds the pages of the dialog; its height follows the visible page (the dialog is "height to content").
class PageStack : public QWidget {
   public:
    explicit PageStack(QWidget* parent) : QWidget(parent) {}

    void addPage(QWidget* page)
    {
        page->setParent(this);
        page->installEventFilter(this);
        page->hide();
    }
    void setCurrent(QWidget* page)
    {
        m_current = page;
        for (QWidget* child : findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
            if (child->property("zeroPage").toBool())
                child->setVisible(child == page);
        page->setGeometry(0, 0, width(), qMax(1, height()));
        syncHeight();
    }
    QWidget* current() const { return m_current; }
    /// Lays the visible page out at the stack's width and makes the stack as tall as it needs.
    void syncHeight()
    {
        if (!m_current)
            return;
        const int h = heightForPage(m_current, width());
        if (h != height())
            setFixedHeight(h);
        m_current->setGeometry(0, 0, width(), h);
    }

   protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (m_current)
            m_current->setGeometry(rect());
    }
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_current && event->type() == QEvent::LayoutRequest)
            QMetaObject::invokeMethod(this, [this]() { syncHeight(); }, Qt::QueuedConnection);
        return QWidget::eventFilter(watched, event);
    }

   private:
    QWidget* m_current = nullptr;
};

/// The slide between two steps: snapshots of the old and the new content move over the live (already changed) page, which
/// stays interactive underneath (DESIGN.md, "Motion language": layouts never animate).
class SlideOverlay : public QWidget {
   public:
    SlideOverlay(QWidget* host, const QPixmap& from, const QPixmap& to, bool forward, int ms)
        : QWidget(host), m_from(from), m_to(to), m_forward(forward), m_anim(this)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(host->rect());
        m_anim.setStartValue(0.0);
        m_anim.setEndValue(1.0);
        m_anim.setDuration(ms);
        m_anim.setEasingCurve(Motion::easeEnter());
        connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_progress = v.toReal();
            update();
        });
        connect(&m_anim, &QVariantAnimation::finished, this, &QObject::deleteLater);
        raise();
        show();
        m_anim.start();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const qreal w = width();
        const qreal dir = m_forward ? 1.0 : -1.0;
        // The old content leaves a third of the way as far as the new one comes: the new step reads as the nearer one.
        p.drawPixmap(QPointF(-dir * m_progress * w, 0), m_from);
        p.drawPixmap(QPointF(dir * (1.0 - m_progress) * w, 0), m_to);
    }

   private:
    QPixmap m_from;
    QPixmap m_to;
    bool m_forward;
    QVariantAnimation m_anim;
    qreal m_progress = 0.0;
};

/// The slides running on `stack` (SlideOverlay has no Q_OBJECT, so findChildren cannot be used).
QList<SlideOverlay*> slideOverlays(QWidget* stack)
{
    QList<SlideOverlay*> overlays;
    for (QObject* child : stack->children())
        if (auto* overlay = dynamic_cast<SlideOverlay*>(child))
            overlays.append(overlay);
    return overlays;
}

// ---------------------------------------------------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------------------------------------------------

AddAccountEnvironment AddAccountEnvironment::fromApplication()
{
    AddAccountEnvironment env;
    env.microsoft = !APPLICATION->getMSAClientID().isEmpty();
    env.ely = (APPLICATION->capabilities() & Application::SupportsEly) != 0;
    env.elyCode = (APPLICATION->capabilities() & Application::SupportsElyOAuth) != 0;
    env.offlineAllowed = APPLICATION->accounts()->anyAccountIsValid();
    env.makeFlow = [](AccountData* data, AuthFlow::Action action) { return shared_qobject_ptr<AuthFlow>(new AuthFlow(data, action)); };
    env.openUrl = [](const QUrl& url) { return DesktopServices::openUrl(url); };
    env.copyText = [](const QString& text) { QApplication::clipboard()->setText(text); };
    env.makeOffline = [](const QString& name) -> MinecraftAccountPtr {
        MinecraftAccountPtr account = MinecraftAccount::createOffline(name);
        if (account)
            account->login()->start();  // no steps: completes at once (as the old Add Offline did)
        return account;
    };
    return env;
}

// ---------------------------------------------------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------------------------------------------------

MinecraftAccountPtr AddAccountDialog::run(QWidget* parent)
{
    AddAccountDialog dialog(parent);
    return dialog.exec() == QDialog::Accepted ? dialog.m_account : nullptr;
}

MinecraftAccountPtr AddAccountDialog::run(QWidget* parent, AccountType type)
{
    return run(parent, type, SignInHint{});
}

MinecraftAccountPtr AddAccountDialog::run(QWidget* parent, AccountType type, const SignInHint& hint)
{
    AddAccountDialog dialog(AddAccountEnvironment::fromApplication(), type, hint, parent);
    return dialog.exec() == QDialog::Accepted ? dialog.m_account : nullptr;
}

AddAccountDialog::AddAccountDialog(QWidget* parent) : AddAccountDialog(AddAccountEnvironment::fromApplication(), std::nullopt, parent) {}

AddAccountDialog::AddAccountDialog(AddAccountEnvironment env, QWidget* parent) : AddAccountDialog(std::move(env), std::nullopt, parent) {}

AddAccountDialog::AddAccountDialog(AddAccountEnvironment env, std::optional<AccountType> startAt, QWidget* parent)
    : AddAccountDialog(std::move(env), startAt, SignInHint{}, parent)
{}

AddAccountDialog::AddAccountDialog(AddAccountEnvironment env, std::optional<AccountType> startAt, const SignInHint& hint, QWidget* parent)
    : QDialog(parent), m_env(std::move(env)), m_hint(hint)
{
    build(startAt);
}

AddAccountDialog::~AddAccountDialog()
{
    abortFlows();
    clearPendingPassword();
}

bool AddAccountDialog::isValidOfflineName(const QString& name)
{
    static const QRegularExpression valid(QStringLiteral("\\A[A-Za-z0-9_]{3,16}\\z"));  // \z: a trailing newline is not valid
    return valid.match(name).hasMatch();
}

void AddAccountDialog::build(std::optional<AccountType> startAt)
{
    setObjectName(QStringLiteral("ZeroAddAccountDialog"));
    setWindowTitle(tr("Add account"));
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl);
    root->setSpacing(0);
    root->setSizeConstraint(QLayout::SetFixedSize);

    m_stack = new PageStack(this);
    m_stack->setFixedWidth(kContentWidth);
    m_stack->setFixedHeight(1);
    root->addWidget(m_stack);

    m_choosePage = buildChoosePage();
    m_signInPage = buildSignInPage();
    m_offlinePage = buildOfflinePage();
    m_passwordPage = buildPasswordPage();
    for (QWidget* page : { m_choosePage, m_signInPage, m_offlinePage, m_passwordPage }) {
        page->setProperty("zeroPage", true);
        m_stack->addPage(page);
    }

    m_expiry.setSingleShot(true);
    connect(&m_expiry, &QTimer::timeout, this, [this]() {
        if (m_state == SignInState::Waiting)
            showError(tr("The code expired. Try again."));
    });
    m_closeTimer.setSingleShot(true);
    connect(&m_closeTimer, &QTimer::timeout, this, [this]() {
        if (m_state == SignInState::Succeeded)
            accept();
    });
    m_copiedTimer.setSingleShot(true);
    connect(&m_copiedTimer, &QTimer::timeout, this, [this]() { m_codePanel->setCopied(false); });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcons();
        update();
    });

    if (startAt) {
        m_direct = true;
        goTo(pageFor(*startAt), *startAt, true, false);
    } else {
        goTo(Page::Choose, AccountType::MSA, true, false);
    }
}

QWidget* AddAccountDialog::buildChoosePage()
{
    auto* page = new QWidget(m_stack);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    m_chooseOverline = makeLabel(tr("Step 1 of 2"), Role::Overline, page, nullptr, false);
    column->addWidget(m_chooseOverline);
    column->addSpacing(Tokens::spaceXs);
    m_chooseTitle = makeLabel(tr("Choose your account"), Role::Title, page, nullptr, false);
    column->addWidget(m_chooseTitle);
    column->addSpacing(Tokens::spaceXs);
    column->addWidget(makeLabel(tr("You can add more later."), Role::Body, page, "secondary", false));
    column->addSpacing(Tokens::spaceLg);

    struct Option {
        AccountType type;
        bool offered;
        const char* icon;
        QString title;
        QString caption;
    };
    const Option options[] = {
        { AccountType::MSA, m_env.microsoft, "key-round", tr("Microsoft"), tr("Official account. Every server.") },
        { AccountType::Ely, m_env.ely, "gem", tr("Ely.by"), tr("Free account with skins. Servers that accept Ely.by.") },
        { AccountType::Offline, true, "user-x", tr("Offline"), tr("Just a name. No online servers.") },
    };
    bool first = true;
    for (const Option& option : options) {
        if (!option.offered)
            continue;
        if (!first)
            column->addSpacing(Tokens::spaceSm);
        first = false;
        auto* card = new OptionCard(option.type, QString::fromLatin1(option.icon), option.title, option.caption, page);
        const AccountType type = option.type;
        connect(card, &ZeroCard::clicked, this, [this, type]() { chooseType(type); });
        column->addWidget(card);
        m_cards.append(card);
        m_cardTypes.append(type);
    }
    // Tab order follows the cards
    for (int i = 1; i < m_cards.size(); ++i)
        setTabOrder(m_cards.at(i - 1), m_cards.at(i));
    return page;
}

QWidget* AddAccountDialog::buildSignInPage()
{
    auto* page = new QWidget(m_stack);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto* header = new QHBoxLayout;
    header->setSpacing(Tokens::spaceSm);
    m_signInBack = new QToolButton(page);
    m_signInBack->setAccessibleName(tr("Back"));
    m_signInBack->setToolTip(tr("Back"));
    m_signInBack->setFixedSize(Tokens::controlHeight, Tokens::controlHeight);
    m_signInBack->setIconSize(QSize(16, 16));
    Style::setProperty(m_signInBack, "zeroVariant", "ghost");
    connect(m_signInBack, &QToolButton::clicked, this, &AddAccountDialog::chooseStep);
    header->addWidget(m_signInBack);
    m_signInProvider = makeLabel(QString(), Role::Label, page, "secondary", false);
    header->addWidget(m_signInProvider, 1);
    column->addLayout(header);
    column->addSpacing(Tokens::spaceSm);

    m_signInOverline = makeLabel(tr("Step 2 of 2"), Role::Overline, page, nullptr, false);
    column->addWidget(m_signInOverline);
    column->addSpacing(Tokens::spaceXs);
    m_signInTitle = makeLabel(QString(), Role::Title, page, nullptr, false);
    column->addWidget(m_signInTitle);
    column->addSpacing(Tokens::spaceLg);

    m_primary = new QPushButton(page);
    Style::setProperty(m_primary, "zeroVariant", "primary");
    Style::setProperty(m_primary, "zeroSize", "lg");
    m_primary->setDefault(true);
    m_primary->setAutoDefault(true);
    connect(m_primary, &QPushButton::clicked, this, &AddAccountDialog::openSignInPage);
    column->addWidget(m_primary);
    column->addSpacing(Tokens::spaceSm);

    m_caption = makeLabel(QString(), Role::Caption, page, "secondary");
    m_caption->setAlignment(Qt::AlignHCenter);
    column->addWidget(m_caption);
    column->addSpacing(Tokens::spaceSm);

    m_codePanel = new CodePanel(page);
    connect(m_codePanel->copyButton(), &QAbstractButton::clicked, this, &AddAccountDialog::copyCode);
    column->addWidget(m_codePanel);
    column->addSpacing(Tokens::spaceSm);

    // Ely.by only: the code flow needs a client ID, email + password never does
    m_codeUsePassword = new LinkButton(tr("Use email and password instead"), QStringLiteral("key-round"), page);
    connect(m_codeUsePassword, &QAbstractButton::clicked, this, [this]() { goTo(Page::Password, AccountType::Ely, true, true); });
    column->addWidget(m_codeUsePassword, 0, Qt::AlignLeft);
    column->addSpacing(Tokens::spaceSm);

    auto* statusRow = new QHBoxLayout;
    statusRow->setSpacing(Tokens::spaceSm);
    m_glyph = new StatusGlyph(page);
    statusRow->addWidget(m_glyph, 0, Qt::AlignTop);
    m_status = makeLabel(QString(), Role::Body, page, "secondary");
    m_status->setMinimumHeight(kGlyphHeight + Tokens::spaceSm);
    statusRow->addWidget(m_status, 1);
    m_retry = new QPushButton(tr("Try again"), page);
    Style::setProperty(m_retry, "zeroVariant", "secondary");
    m_retry->setAutoDefault(true);
    m_retry->hide();
    connect(m_retry, &QPushButton::clicked, this, [this]() { startFlows(); });
    statusRow->addWidget(m_retry, 0, Qt::AlignTop);
    column->addLayout(statusRow);

    setTabOrder(m_signInBack, m_primary);
    setTabOrder(m_primary, m_codePanel->copyButton());
    setTabOrder(m_codePanel->copyButton(), m_codeUsePassword);
    setTabOrder(m_codeUsePassword, m_retry);
    return page;
}

QWidget* AddAccountDialog::buildPasswordPage()
{
    auto* page = new QWidget(m_stack);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto* header = new QHBoxLayout;
    header->setSpacing(Tokens::spaceSm);
    m_pwBack = new QToolButton(page);
    m_pwBack->setAccessibleName(tr("Back"));
    m_pwBack->setToolTip(tr("Back"));
    m_pwBack->setFixedSize(Tokens::controlHeight, Tokens::controlHeight);
    m_pwBack->setIconSize(QSize(16, 16));
    Style::setProperty(m_pwBack, "zeroVariant", "ghost");
    connect(m_pwBack, &QToolButton::clicked, this, &AddAccountDialog::chooseStep);
    header->addWidget(m_pwBack);
    m_pwProvider = makeLabel(tr("Ely.by"), Role::Label, page, "secondary", false);
    header->addWidget(m_pwProvider, 1);
    column->addLayout(header);
    column->addSpacing(Tokens::spaceSm);

    m_pwOverline = makeLabel(tr("Step 2 of 2"), Role::Overline, page, nullptr, false);
    column->addWidget(m_pwOverline);
    column->addSpacing(Tokens::spaceXs);
    m_pwTitle = makeLabel(tr("Sign in with Ely.by"), Role::Title, page, nullptr, false);
    column->addWidget(m_pwTitle);
    column->addSpacing(Tokens::spaceLg);

    auto* userLabel = makeLabel(tr("Email or username"), Role::Overline, page, nullptr, false);
    column->addWidget(userLabel);
    column->addSpacing(2);
    m_pwUser = new QLineEdit(page);
    m_pwUser->setAccessibleName(tr("Email or username"));
    m_pwUser->setPlaceholderText(tr("Email or username"));
    m_pwUser->setText(m_hint.username);
    userLabel->setBuddy(m_pwUser);
    column->addWidget(m_pwUser);
    column->addSpacing(Tokens::spaceMd);

    auto* passLabel = makeLabel(tr("Password"), Role::Overline, page, nullptr, false);
    column->addWidget(passLabel);
    column->addSpacing(2);
    m_pwPass = new QLineEdit(page);
    m_pwPass->setEchoMode(QLineEdit::Password);
    m_pwPass->setAccessibleName(tr("Password"));
    m_pwPass->setPlaceholderText(tr("Password"));
    m_pwPass->setInputMethodHints(Qt::ImhSensitiveData | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase);
    m_pwEye = m_pwPass->addAction(QIcon(), QLineEdit::TrailingPosition);
    m_pwEye->setCheckable(true);
    m_pwEye->setToolTip(tr("Show password"));
    connect(m_pwEye, &QAction::toggled, this, [this](bool shown) {
        m_pwPass->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
        m_pwEye->setToolTip(shown ? tr("Hide password") : tr("Show password"));
        refreshIcons();
    });
    passLabel->setBuddy(m_pwPass);
    column->addWidget(m_pwPass);

    // The two-factor code: closed until Ely.by asks for it, then it slides open under the password.
    m_twoFactorBox = new QWidget(page);
    auto* codeColumn = new QVBoxLayout(m_twoFactorBox);
    codeColumn->setContentsMargins(0, Tokens::spaceMd, 0, 0);
    codeColumn->setSpacing(0);
    auto* codeLabel = makeLabel(tr("Two-factor code"), Role::Overline, m_twoFactorBox, nullptr, false);
    codeColumn->addWidget(codeLabel);
    codeColumn->addSpacing(2);
    m_pwCode = new QLineEdit(m_twoFactorBox);
    m_pwCode->setAccessibleName(tr("Two-factor code"));
    m_pwCode->setPlaceholderText(tr("6 digits"));
    m_pwCode->setMaxLength(6);
    m_pwCode->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,6}")), m_pwCode));
    m_pwCode->setInputMethodHints(Qt::ImhDigitsOnly | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText);
    codeLabel->setBuddy(m_pwCode);
    codeColumn->addWidget(m_pwCode);
    codeColumn->addSpacing(Tokens::spaceXs);
    m_pwCodeCaption = makeLabel(tr("Enter the code from your authenticator app."), Role::Caption, m_twoFactorBox, "secondary");
    codeColumn->addWidget(m_pwCodeCaption);
    m_twoFactorBox->setMaximumHeight(0);
    m_twoFactorBox->hide();
    column->addWidget(m_twoFactorBox);
    column->addSpacing(Tokens::spaceLg);

    m_pwSubmit = new QPushButton(tr("Sign in"), page);
    Style::setProperty(m_pwSubmit, "zeroVariant", "primary");
    Style::setProperty(m_pwSubmit, "zeroSize", "lg");
    m_pwSubmit->setDefault(true);
    m_pwSubmit->setAutoDefault(true);
    connect(m_pwSubmit, &QPushButton::clicked, this, &AddAccountDialog::submitPassword);
    column->addWidget(m_pwSubmit);
    column->addSpacing(Tokens::spaceMd);

    auto* statusRow = new QHBoxLayout;
    statusRow->setSpacing(Tokens::spaceSm);
    m_pwGlyph = new StatusGlyph(page);
    m_pwGlyph->hide();
    statusRow->addWidget(m_pwGlyph, 0, Qt::AlignTop);
    m_pwStatus = makeLabel(QString(), Role::Body, page, "secondary");
    m_pwStatus->setMinimumHeight(kGlyphHeight + Tokens::spaceSm);
    statusRow->addWidget(m_pwStatus, 1);
    column->addLayout(statusRow);
    column->addSpacing(Tokens::spaceMd);

    m_pwPrivacy = makeLabel(tr("ZERO sends your password only to Ely.by and never saves it."), Role::Caption, page, "tertiary");
    column->addWidget(m_pwPrivacy);
    column->addSpacing(Tokens::spaceXs);
    auto* links = new QHBoxLayout;
    links->setSpacing(Tokens::spaceMd);
    m_pwCreate = new LinkButton(tr("Create an Ely.by account"), QStringLiteral("external-link"), page);
    connect(m_pwCreate, &QAbstractButton::clicked, this, [this]() { openLink(QUrl(QStringLiteral("https://account.ely.by/register"))); });
    links->addWidget(m_pwCreate);
    m_pwForgot = new LinkButton(tr("Forgot password?"), QStringLiteral("external-link"), page);
    connect(m_pwForgot, &QAbstractButton::clicked, this,
            [this]() { openLink(QUrl(QStringLiteral("https://account.ely.by/forgot-password"))); });
    links->addWidget(m_pwForgot);
    links->addStretch(1);
    column->addLayout(links);
    m_pwUseCode = new LinkButton(tr("Use a code instead"), QStringLiteral("link"), page);
    connect(m_pwUseCode, &QAbstractButton::clicked, this, [this]() { goTo(Page::SignIn, AccountType::Ely, false, true); });
    column->addWidget(m_pwUseCode, 0, Qt::AlignLeft);

    m_twoFactorAnim = new QVariantAnimation(this);
    m_twoFactorAnim->setStartValue(0);
    m_twoFactorAnim->setEasingCurve(Motion::easeEnter());
    connect(m_twoFactorAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_twoFactorBox->setMaximumHeight(v.toInt());
        if (m_stack)
            m_stack->syncHeight();
    });
    connect(m_twoFactorAnim, &QVariantAnimation::finished, this, [this]() {
        if (m_twoFactorShown)
            m_twoFactorBox->setMaximumHeight(QWIDGETSIZE_MAX);
        if (m_stack)
            m_stack->syncHeight();
    });

    setTabOrder(m_pwBack, m_pwUser);
    setTabOrder(m_pwUser, m_pwPass);
    setTabOrder(m_pwPass, m_pwCode);
    setTabOrder(m_pwCode, m_pwSubmit);
    setTabOrder(m_pwSubmit, m_pwCreate);
    setTabOrder(m_pwCreate, m_pwForgot);
    setTabOrder(m_pwForgot, m_pwUseCode);
    return page;
}

// ---- Ely.by password form ------------------------------------------------------------------------------------------

bool AddAccountDialog::elyUsesPassword() const
{
    return !m_env.elyCode || (m_direct && m_hint.password);
}

AddAccountDialog::Page AddAccountDialog::pageFor(AccountType type) const
{
    if (type == AccountType::Offline)
        return Page::Offline;
    if (type == AccountType::Ely && elyUsesPassword())
        return Page::Password;
    return Page::SignIn;
}

void AddAccountDialog::clearPendingPassword()
{
    m_pendingPassword.fill(QChar(0));
    m_pendingPassword.clear();
}

void AddAccountDialog::openLink(const QUrl& url)
{
    if (!(m_env.openUrl && m_env.openUrl(url)) && m_env.copyText)
        m_env.copyText(url.toString());
}

/// Entering the form: empty password and code, no status, the two-factor field closed (the user name stays).
void AddAccountDialog::resetPasswordPage()
{
    clearPendingPassword();
    m_pwPass->clear();
    m_pwCode->clear();
    m_pwEye->setChecked(false);
    setTwoFactorVisible(false, false);
    setPasswordBusy(false);
    m_pwGlyph->hide();
    m_pwStatus->clear();
    Style::setProperty(m_pwStatus, "zeroTone", QStringLiteral("secondary"));
    m_state = SignInState::Idle;
}

void AddAccountDialog::setPasswordBusy(bool busy)
{
    m_pwUser->setEnabled(!busy);
    m_pwPass->setEnabled(!busy);
    m_pwCode->setEnabled(!busy);
    m_pwSubmit->setEnabled(!busy);
}

void AddAccountDialog::showPasswordStatus(const QString& text, const char* tone, bool error)
{
    m_pwStatus->setText(text);
    Style::setProperty(m_pwStatus, "zeroTone", QString::fromLatin1(tone));
    m_pwGlyph->setMode(error ? StatusGlyph::Mode::Error : StatusGlyph::Mode::Dots);
    m_pwGlyph->setVisible(!text.isEmpty());
    m_stack->syncHeight();
}

void AddAccountDialog::setTwoFactorVisible(bool visible, bool animate)
{
    if (visible == m_twoFactorShown)
        return;
    m_twoFactorShown = visible;
    m_twoFactorAnim->stop();
    if (!visible) {
        m_twoFactorBox->setMaximumHeight(0);
        m_twoFactorBox->hide();
        m_stack->syncHeight();
        return;
    }
    m_twoFactorBox->show();
    const int target = qMax(1, heightForPage(m_twoFactorBox, kContentWidth));
    if (animate && isVisible() && Motion::duration(220) > 0) {
        m_twoFactorBox->setMaximumHeight(0);
        m_twoFactorAnim->setEndValue(target);
        m_twoFactorAnim->setDuration(Motion::duration(220));
        m_twoFactorAnim->start();
    } else {
        m_twoFactorBox->setMaximumHeight(QWIDGETSIZE_MAX);
        m_stack->syncHeight();
    }
}

void AddAccountDialog::submitPassword()
{
    if (m_page != Page::Password || m_state == SignInState::Working || m_state == SignInState::Succeeded)
        return;
    const QString username = m_pwUser->text().trimmed();
    // The field is emptied on every submit; while Ely.by may still ask for the code the password is kept here (memory only).
    QString password = m_pwPass->text();
    if (password.isEmpty())
        password = m_pendingPassword;
    const QString code = m_twoFactorShown ? m_pwCode->text().trimmed() : QString();
    if (username.isEmpty()) {
        m_pwUser->setFocus(Qt::TabFocusReason);
        return;
    }
    if (password.isEmpty()) {
        m_pwPass->setFocus(Qt::TabFocusReason);
        return;
    }
    if (m_twoFactorShown && code.isEmpty()) {
        m_pwCode->setFocus(Qt::TabFocusReason);
        return;
    }

    abortFlows();
    m_pendingPassword = password;
    m_pwPass->clear();
    m_account = MinecraftAccount::createBlankElyPassword();
    shared_qobject_ptr<AuthFlow> task = m_env.makeFlow ? m_env.makeFlow(m_account->accountData(), AuthFlow::Action::Login)
                                                         : shared_qobject_ptr<AuthFlow>(new AuthFlow(m_account->accountData(), AuthFlow::Action::Login));
    if (!task) {
        clearPendingPassword();
        showPasswordStatus(tr("Sign-in is not available in this build."), "danger", true);
        m_state = SignInState::Failed;
        return;
    }
    AuthFlow* raw = task.get();
    Flow flow;
    flow.task = task;
    m_flows.push_back(flow);
    connect(raw, &Task::succeeded, this, [this, raw]() { onPasswordSucceeded(raw); });
    connect(raw, &Task::failed, this, [this, raw](QString reason) { onPasswordFailed(raw, reason); });
    connect(raw, &Task::aborted, this, [this, raw]() { onPasswordFailed(raw, QString()); });
    raw->setElyCredentials(username, password, code);

    m_state = SignInState::Working;
    setPasswordBusy(true);
    m_pwStatus->setText(tr("Signing in\xE2\x80\xA6"));
    Style::setProperty(m_pwStatus, "zeroTone", QStringLiteral("secondary"));
    m_pwGlyph->setMode(StatusGlyph::Mode::Dots);
    m_pwGlyph->show();
    m_stack->syncHeight();
    const auto generation = ++m_generation;
    if (generation == m_generation && flowFor(raw))
        raw->start();
}

void AddAccountDialog::onPasswordSucceeded(AuthFlow* task)
{
    Flow* flow = flowFor(task);
    if (!flow || m_state == SignInState::Succeeded)
        return;
    flow->finished = true;
    clearPendingPassword();
    m_state = SignInState::Succeeded;
    m_pwBack->setEnabled(false);
    m_pwGlyph->setMode(StatusGlyph::Mode::Check);
    m_pwGlyph->show();
    const QString name = m_account ? m_account->profileName() : QString();
    m_pwStatus->setText(name.isEmpty() ? tr("Signed in") : tr("Signed in as %1").arg(name));
    Style::setProperty(m_pwStatus, "zeroTone", QStringLiteral("success"));
    m_stack->syncHeight();
    m_closeTimer.start(m_env.successDelayMs);
}

void AddAccountDialog::onPasswordFailed(AuthFlow* task, const QString& reason)
{
    using Zero::Auth::Ely::Authserver::Failure;
    Flow* flow = flowFor(task);
    if (!flow || m_state != SignInState::Working)
        return;
    flow->finished = true;
    const Failure failure = task->elyFailure();
    m_state = SignInState::Failed;
    setPasswordBusy(false);

    switch (failure) {
        case Failure::TwoFactorRequired:
            // not an error: the account wants a code. The password stays in memory for the second attempt.
            m_state = SignInState::Idle;
            showPasswordStatus(QString(), "secondary", false);
            setTwoFactorVisible(true, true);
            m_pwCode->setFocus(Qt::TabFocusReason);
            return;
        case Failure::WrongCode:
            m_pwCode->clear();
            showPasswordStatus(tr("That code didn't work. Try again."), "danger", true);
            m_pwCode->setFocus(Qt::TabFocusReason);
            return;
        case Failure::WrongCredentials:
            clearPendingPassword();
            setTwoFactorVisible(false, false);
            m_pwCode->clear();
            showPasswordStatus(tr("Wrong email or password."), "danger", true);
            m_pwPass->setFocus(Qt::TabFocusReason);
            return;
        case Failure::Network:
            clearPendingPassword();
            showPasswordStatus(tr("We couldn't reach Ely.by. Check your connection and try again."), "danger", true);
            m_pwPass->setFocus(Qt::TabFocusReason);
            return;
        case Failure::Refused:
        case Failure::None:
            break;
    }
    clearPendingPassword();
    const QString line = firstLine(reason);
    showPasswordStatus(line.isEmpty() ? tr("Sign-in did not work. Try again.") : line, "danger", true);
    m_pwPass->setFocus(Qt::TabFocusReason);
}

QWidget* AddAccountDialog::buildOfflinePage()
{
    auto* page = new QWidget(m_stack);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto* header = new QHBoxLayout;
    header->setSpacing(Tokens::spaceSm);
    m_offlineBack = new QToolButton(page);
    m_offlineBack->setAccessibleName(tr("Back"));
    m_offlineBack->setToolTip(tr("Back"));
    m_offlineBack->setFixedSize(Tokens::controlHeight, Tokens::controlHeight);
    m_offlineBack->setIconSize(QSize(16, 16));
    Style::setProperty(m_offlineBack, "zeroVariant", "ghost");
    connect(m_offlineBack, &QToolButton::clicked, this, &AddAccountDialog::chooseStep);
    header->addWidget(m_offlineBack);
    m_offlineProvider = makeLabel(tr("Offline"), Role::Label, page, "secondary", false);
    header->addWidget(m_offlineProvider, 1);
    column->addLayout(header);
    column->addSpacing(Tokens::spaceSm);

    m_offlineOverline = makeLabel(tr("Step 2 of 2"), Role::Overline, page, nullptr, false);
    column->addWidget(m_offlineOverline);
    column->addSpacing(Tokens::spaceXs);
    m_offlineTitle = makeLabel(tr("Play offline"), Role::Title, page, nullptr, false);
    column->addWidget(m_offlineTitle);
    column->addSpacing(Tokens::spaceLg);

    auto* fieldLabel = makeLabel(tr("Username"), Role::Overline, page, nullptr, false);
    column->addWidget(fieldLabel);
    column->addSpacing(2);
    m_name = new QLineEdit(page);
    m_name->setAccessibleName(tr("Username"));
    m_name->setPlaceholderText(tr("Username"));
    fieldLabel->setBuddy(m_name);
    column->addWidget(m_name);
    column->addSpacing(Tokens::spaceXs);
    m_nameHint = makeLabel(QString(), Role::Caption, page, "secondary");
    // Reserve two lines for the longest message so nothing jumps while typing.
    m_nameHint->setMinimumHeight(2 * QFontMetrics(Style::font(Role::Caption)).lineSpacing());
    m_nameHint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    column->addWidget(m_nameHint);
    column->addSpacing(Tokens::spaceSm);
    m_allowInvalid = new QCheckBox(tr("Allow invalid usernames"), page);
    column->addWidget(m_allowInvalid);
    column->addSpacing(Tokens::spaceLg);

    m_add = new QPushButton(tr("Add account"), page);
    Style::setProperty(m_add, "zeroVariant", "primary");
    Style::setProperty(m_add, "zeroSize", "lg");
    m_add->setDefault(true);
    m_add->setAutoDefault(true);
    column->addWidget(m_add);

    connect(m_name, &QLineEdit::textChanged, this, &AddAccountDialog::refreshOfflineState);
    connect(m_allowInvalid, &QCheckBox::toggled, this, &AddAccountDialog::refreshOfflineState);
    connect(m_add, &QPushButton::clicked, this, &AddAccountDialog::addOffline);

    setTabOrder(m_offlineBack, m_name);
    setTabOrder(m_name, m_allowInvalid);
    setTabOrder(m_allowInvalid, m_add);
    refreshOfflineState();
    return page;
}

// ---- navigation ----------------------------------------------------------------------------------------------------

void AddAccountDialog::chooseType(AccountType type)
{
    goTo(pageFor(type), type, true, true);
}

void AddAccountDialog::chooseStep()
{
    if (m_direct || m_page == Page::Choose)
        return;
    goTo(Page::Choose, m_type, false, true);
}

QList<AccountType> AddAccountDialog::offeredTypes() const
{
    return m_cardTypes;
}

ZeroCard* AddAccountDialog::optionCard(AccountType type) const
{
    const int index = m_cardTypes.indexOf(type);
    return index >= 0 ? m_cards.at(index) : nullptr;
}

void AddAccountDialog::goTo(Page page, AccountType type, bool forward, bool animate)
{
    const QPoint origin = m_stack->mapTo(this, QPoint(0, 0));
    const bool slide = animate && isVisible() && Motion::duration(kSlideMs) > 0;
    // A slide still running is cut short: the live page is already the target of the last move.
    for (auto* overlay : slideOverlays(m_stack))
        delete overlay;
    QPixmap before;
    if (slide)
        before = grab(QRect(origin, m_stack->size()));

    if (m_page == Page::SignIn || m_page == Page::Password)
        abortFlows();
    if (m_page == Page::Password && page != Page::Password)
        clearPendingPassword();
    m_page = page;
    m_type = type;
    if (page != Page::SignIn) {
        m_closeTimer.stop();
        m_copiedTimer.stop();
        setState(SignInState::Idle);
        m_account.reset();  // a blank account of a sign-in that was left is not an answer
    }
    if (page == Page::Password)
        resetPasswordPage();
    if (page == Page::Offline) {
        m_name->clear();
        m_allowInvalid->setChecked(false);
    }
    refreshPageTexts();
    refreshIcons();

    QWidget* target = page == Page::Choose     ? m_choosePage
                      : page == Page::SignIn   ? m_signInPage
                      : page == Page::Password ? m_passwordPage
                                               : m_offlinePage;
    m_stack->setCurrent(target);
    if (page == Page::SignIn) {
        m_account.reset();
        startFlows();
    } else if (page == Page::Offline) {
        refreshOfflineState();
    }

    if (page == Page::Choose) {
        if (!m_cards.isEmpty())
            m_cards.first()->setFocus(Qt::TabFocusReason);
    } else if (page == Page::SignIn) {
        m_primary->setFocus(Qt::TabFocusReason);
    } else if (page == Page::Password) {
        (m_pwUser->text().isEmpty() ? m_pwUser : m_pwPass)->setFocus(Qt::TabFocusReason);
    } else {
        m_name->setFocus(Qt::TabFocusReason);
    }

    if (slide) {
        layout()->activate();
        QCoreApplication::sendPostedEvents(this, QEvent::LayoutRequest);
        m_stack->syncHeight();
        const QPixmap after = grab(QRect(m_stack->mapTo(this, QPoint(0, 0)), m_stack->size()));
        new SlideOverlay(m_stack, before, after, forward, Motion::duration(kSlideMs));
    }
}

void AddAccountDialog::refreshPageTexts()
{
    const bool showStep = !m_direct;
    m_signInOverline->setVisible(showStep);
    m_offlineOverline->setVisible(showStep);
    m_pwOverline->setVisible(showStep);
    m_signInBack->setVisible(!m_direct);
    m_offlineBack->setVisible(!m_direct);
    m_pwBack->setVisible(!m_direct);
    m_pwBack->setEnabled(true);
    m_pwUseCode->setVisible(m_env.elyCode);  // the code flow exists only with a client ID
    m_codeUsePassword->setVisible(m_type == AccountType::Ely);
    m_signInProvider->setText(providerName());

    const bool ely = m_type == AccountType::Ely;
    m_signInTitle->setText(ely ? tr("Sign in with Ely.by") : tr("Sign in with Microsoft"));
    m_primary->setText(ely ? tr("Open Ely.by") : tr("Open Microsoft sign-in"));
    m_primary->setAccessibleName(m_primary->text());
    m_caption->setText(ely ? tr("or enter this code at ely.by/code") : tr("or enter this code at microsoft.com/link"));
    setWindowTitle(tr("Add account"));
}

void AddAccountDialog::refreshIcons()
{
    const auto& t = Theme::current();
    const QIcon back = Icons::icon(QStringLiteral("arrow-left"), t.textSecondary);
    m_signInBack->setIcon(back);
    m_offlineBack->setIcon(back);
    m_pwBack->setIcon(back);
    if (m_pwEye)
        m_pwEye->setIcon(Icons::icon(m_pwEye->isChecked() ? QStringLiteral("eye-off") : QStringLiteral("eye"), t.textSecondary));
    for (OptionCard* card : std::as_const(m_cards))
        card->refreshIcons();
}

QString AddAccountDialog::providerName() const
{
    switch (m_type) {
        case AccountType::Ely:
            return tr("Ely.by");
        case AccountType::Offline:
            return tr("Offline");
        default:
            return tr("Microsoft");
    }
}

void AddAccountDialog::moveCardFocus(int step)
{
    int index = -1;
    for (int i = 0; i < m_cards.size(); ++i)
        if (focusWidget() == m_cards.at(i))
            index = i;
    const int next = index < 0 ? (step > 0 ? 0 : m_cards.size() - 1) : index + step;
    if (next >= 0 && next < m_cards.size())
        m_cards.at(next)->setFocus(Qt::TabFocusReason);
}

// ---- sign-in -------------------------------------------------------------------------------------------------------

void AddAccountDialog::resetSignInUi()
{
    m_expiry.stop();
    m_copiedTimer.stop();
    m_browserUrl.clear();
    m_deviceLink.clear();
    m_deviceCode.clear();
    m_failure.clear();
    m_codePanel->clear();
    m_primary->setToolTip(QString());
    setState(SignInState::Starting);
}

void AddAccountDialog::setState(SignInState state)
{
    m_state = state;
    const bool dots = state == SignInState::Starting || state == SignInState::Waiting || state == SignInState::Working;
    m_glyph->setMode(state == SignInState::Succeeded ? StatusGlyph::Mode::Check
                                                      : state == SignInState::Failed ? StatusGlyph::Mode::Error : StatusGlyph::Mode::Dots);
    m_glyph->setVisible(state != SignInState::Idle);
    const char* tone = state == SignInState::Succeeded ? "success" : state == SignInState::Failed ? "danger" : "secondary";
    Style::setProperty(m_status, "zeroTone", QString::fromLatin1(tone));
    m_retry->setVisible(state == SignInState::Failed);
    m_signInBack->setEnabled(state != SignInState::Succeeded);  // signed in: the account is real, the dialog is about to close
    m_codePanel->setEnabled(state != SignInState::Failed);
    const bool haveLink = m_browserUrl.isValid() || !m_deviceLink.isEmpty();
    m_primary->setEnabled(dots && haveLink);
    switch (state) {
        case SignInState::Starting:
            m_status->setText(tr("Preparing sign-in\xE2\x80\xA6"));
            break;
        case SignInState::Waiting:
            m_status->setText(tr("Waiting for you to confirm"));
            break;
        default:
            break;
    }
    if (m_stack)
        m_stack->syncHeight();
}

void AddAccountDialog::showStatus(const QString& text)
{
    m_status->setText(text);
    m_stack->syncHeight();
}

void AddAccountDialog::showError(const QString& message)
{
    m_expiry.stop();
    m_failure = message.isEmpty() ? tr("Sign-in did not work. Try again.") : message;
    abortFlows();
    setState(SignInState::Failed);
    m_status->setText(m_failure);
    m_stack->syncHeight();
    m_retry->setFocus(Qt::TabFocusReason);
}

AddAccountDialog::Flow* AddAccountDialog::flowFor(AuthFlow* task)
{
    for (Flow& flow : m_flows)
        if (flow.task.get() == task)
            return &flow;
    return nullptr;
}

void AddAccountDialog::startFlows()
{
    abortFlows();
    resetSignInUi();
    const bool ely = m_type == AccountType::Ely;
    m_account = ely ? MinecraftAccount::createBlankEly() : MinecraftAccount::createBlankMSA();

    // Microsoft: the browser flow and the device code flow, whichever finishes first; Ely.by: the device code flow only.
    std::vector<AuthFlow::Action> actions;
    if (!ely)
        actions.push_back(AuthFlow::Action::Login);
    actions.push_back(AuthFlow::Action::DeviceCode);

    std::vector<AuthFlow*> started;
    for (AuthFlow::Action action : actions) {
        shared_qobject_ptr<AuthFlow> task =
            m_env.makeFlow ? m_env.makeFlow(m_account->accountData(), action) : shared_qobject_ptr<AuthFlow>(new AuthFlow(m_account->accountData(), action));
        if (!task)
            continue;
        AuthFlow* raw = task.get();
        Flow flow;
        flow.task = task;
        m_flows.push_back(flow);
        connect(raw, &Task::succeeded, this, [this, raw]() { onFlowSucceeded(raw); });
        connect(raw, &Task::failed, this, [this, raw](QString reason) { onFlowFailed(raw, reason); });
        connect(raw, &Task::aborted, this, [this, raw]() { onFlowAborted(raw); });
        connect(raw, &Task::status, this, [this, raw](QString status) { onFlowStatus(raw, status); });
        connect(raw, &AuthFlow::authorizeWithBrowser, this, [this, raw](const QUrl& url) { onBrowserUrl(raw, url); });
        connect(raw, &AuthFlow::authorizeWithBrowserWithExtra, this,
                [this, raw](QString url, QString code, int expiresIn) { onDeviceCode(raw, url, code, expiresIn); });
        started.push_back(raw);
    }
    // Start only after every flow is wired and registered: a flow may answer at once.
    const auto generation = ++m_generation;
    for (AuthFlow* raw : started) {
        if (generation != m_generation || !flowFor(raw))
            break;
        raw->start();
    }
    if (m_flows.empty())
        showError(tr("Sign-in is not available in this build."));
}

void AddAccountDialog::abortFlows()
{
    ++m_generation;
    m_expiry.stop();
    std::vector<Flow> flows = std::move(m_flows);
    m_flows.clear();
    for (Flow& flow : flows) {
        if (!flow.task)
            continue;
        QObject::disconnect(flow.task.get(), nullptr, this, nullptr);
        if (flow.task->isRunning())
            flow.task->abort();
    }
}

int AddAccountDialog::runningFlows() const
{
    int running = 0;
    for (const Flow& flow : m_flows)
        if (flow.task && flow.task->isRunning())
            ++running;
    return running;
}

void AddAccountDialog::onBrowserUrl(AuthFlow* task, const QUrl& url)
{
    Flow* flow = flowFor(task);
    if (!flow || (m_state != SignInState::Starting && m_state != SignInState::Waiting))
        return;
    flow->armed = true;
    m_browserUrl = url;
    m_primary->setToolTip(QStringLiteral("<div style='width: 200px;'>%1</div>").arg(url.toString().toHtmlEscaped()));
    setState(SignInState::Waiting);
}

void AddAccountDialog::onDeviceCode(AuthFlow* task, const QString& url, const QString& code, int expiresIn)
{
    Flow* flow = flowFor(task);
    if (!flow || (m_state != SignInState::Starting && m_state != SignInState::Waiting))
        return;
    flow->armed = true;
    m_deviceCode = code;
    QString link = url;
    if (m_type == AccountType::MSA && url == QLatin1String("https://www.microsoft.com/link") && !code.isEmpty())
        link += QStringLiteral("?otc=%1").arg(code);  // Prism's way: the QR code and the button come with the code filled in
    m_deviceLink = link;
    if (const QString address = displayAddress(QUrl(url)); !address.isEmpty())
        m_caption->setText(tr("or enter this code at %1").arg(address));
    m_codePanel->setCode(code, link);
    if (m_type == AccountType::Ely)
        m_primary->setToolTip(QStringLiteral("<div style='width: 200px;'>%1</div>").arg(url.toHtmlEscaped()));
    if (expiresIn > 0)
        m_expiry.start(expiresIn * 1000);
    setState(SignInState::Waiting);
}

void AddAccountDialog::onFlowStatus(AuthFlow* task, const QString& status)
{
    Flow* flow = flowFor(task);
    if (!flow || !flow->armed || m_state == SignInState::Succeeded || m_state == SignInState::Failed)
        return;
    if (!flow->progressed)
        onUserConfirmed(task);
    showStatus(status);
}

void AddAccountDialog::onUserConfirmed(AuthFlow* task)
{
    // The user confirmed through this flow: from here on it is the only one. The other flows stop (a running flow
    // would keep polling or keep a port open for nothing).
    Flow* mine = flowFor(task);
    if (!mine)
        return;
    mine->progressed = true;
    m_expiry.stop();
    for (Flow& other : m_flows) {
        if (&other == mine || !other.task)
            continue;
        QObject::disconnect(other.task.get(), nullptr, this, nullptr);
        if (other.task->isRunning())
            other.task->abort();
        other.finished = true;
    }
    setState(SignInState::Working);
}

void AddAccountDialog::onFlowSucceeded(AuthFlow* task)
{
    Flow* flow = flowFor(task);
    if (!flow || m_state == SignInState::Succeeded || m_state == SignInState::Failed)
        return;
    flow->finished = true;
    flow->progressed = true;
    m_expiry.stop();
    for (Flow& other : m_flows) {
        if (&other == flow || !other.task)
            continue;
        QObject::disconnect(other.task.get(), nullptr, this, nullptr);
        if (other.task->isRunning())
            other.task->abort();
        other.finished = true;
    }
    setState(SignInState::Succeeded);
    const QString name = m_account ? m_account->profileName() : QString();
    showStatus(name.isEmpty() ? tr("Signed in") : tr("Signed in as %1").arg(name));
    m_closeTimer.start(m_env.successDelayMs);
}

void AddAccountDialog::onFlowFailed(AuthFlow* task, const QString& reason)
{
    Flow* flow = flowFor(task);
    if (!flow || m_state == SignInState::Succeeded || m_state == SignInState::Failed)
        return;
    flow->finished = true;
    const bool othersAlive = std::any_of(m_flows.begin(), m_flows.end(), [](const Flow& f) { return !f.finished; });
    // A flow that failed before the user confirmed leaves the other way open; one that failed after, or the last one, is the answer.
    if (flow->progressed || !othersAlive)
        showError(firstLine(reason));
}

void AddAccountDialog::onFlowAborted(AuthFlow* task)
{
    Flow* flow = flowFor(task);
    if (!flow || m_state == SignInState::Succeeded || m_state == SignInState::Failed)
        return;
    flow->finished = true;
    if (std::none_of(m_flows.begin(), m_flows.end(), [](const Flow& f) { return !f.finished; }))
        showError(tr("Sign-in was cancelled. Try again."));
}

void AddAccountDialog::openSignInPage()
{
    QUrl url = m_browserUrl;
    if (!url.isValid() || url.isEmpty())
        url = QUrl(m_deviceLink);
    if (!url.isValid() || url.isEmpty())
        return;
    // Like Prism's sign-in button: when no browser opens, the link goes to the clipboard.
    if (!(m_env.openUrl && m_env.openUrl(url)) && m_env.copyText)
        m_env.copyText(url.toString());
}

void AddAccountDialog::copyCode()
{
    if (m_deviceCode.isEmpty())
        return;
    if (m_env.copyText)
        m_env.copyText(m_deviceCode);
    m_codePanel->setCopied(true);
    m_copiedTimer.start(kCopiedMs);
}

// ---- offline -------------------------------------------------------------------------------------------------------

void AddAccountDialog::refreshOfflineState()
{
    if (!m_name)
        return;
    const QString text = m_name->text();
    const bool valid = isValidOfflineName(text);
    const bool allowed = m_env.offlineAllowed;
    m_name->setEnabled(allowed);
    m_allowInvalid->setEnabled(allowed);
    const bool allowInvalid = m_allowInvalid->isChecked();
    QString hint;
    const char* tone = "secondary";
    if (!allowed) {
        hint = tr("You must add an account that owns Minecraft before you can add an offline account.");
        tone = "danger";
    } else if (!text.isEmpty() && !valid) {
        if (allowInvalid) {
            hint = tr("An invalid username may prevent joining servers and singleplayer worlds.");
            tone = "warning";
        } else {
            hint = tr("A username is valid only if it is from 3 to 16 characters in length, uses English letters, numbers, and underscores.");
            tone = "danger";
        }
    } else {
        hint = tr("3 to 16 English letters, numbers or underscores.");
    }
    m_nameHint->setText(hint);
    Style::setProperty(m_nameHint, "zeroTone", QString::fromLatin1(tone));
    m_add->setEnabled(allowed && (allowInvalid ? !text.isEmpty() : valid));
}

void AddAccountDialog::addOffline()
{
    if (!m_add->isEnabled())
        return;
    const QString name = m_name->text();
    MinecraftAccountPtr account = m_env.makeOffline ? m_env.makeOffline(name) : MinecraftAccount::createOffline(name);
    if (!account) {
        m_nameHint->setText(tr("The account could not be created."));
        Style::setProperty(m_nameHint, "zeroTone", QStringLiteral("danger"));
        return;
    }
    m_account = account;
    accept();
}

// ---- parts, events -------------------------------------------------------------------------------------------------

QLabel* AddAccountDialog::titleLabel() const
{
    return m_page == Page::Choose ? m_chooseTitle : m_page == Page::SignIn ? m_signInTitle : m_page == Page::Password ? m_pwTitle : m_offlineTitle;
}

QLabel* AddAccountDialog::overlineLabel() const
{
    return m_page == Page::Choose     ? m_chooseOverline
           : m_page == Page::SignIn   ? m_signInOverline
           : m_page == Page::Password ? m_pwOverline
                                      : m_offlineOverline;
}

QToolButton* AddAccountDialog::backButton() const
{
    return m_page == Page::Offline ? m_offlineBack : m_page == Page::Password ? m_pwBack : m_signInBack;
}

QWidget* AddAccountDialog::codePanel() const
{
    return m_codePanel;
}

QLabel* AddAccountDialog::codeLabel() const
{
    return m_codePanel->codeLabel();
}

QAbstractButton* AddAccountDialog::copyButton() const
{
    return m_codePanel->copyButton();
}

QString AddAccountDialog::qrText() const
{
    return m_codePanel->qrText();
}

int AddAccountDialog::slideOverlayCount() const
{
    return int(slideOverlays(m_stack).size());
}

void AddAccountDialog::done(int result)
{
    // Esc or the close button during the short "Signed in as ..." moment: the sign-in did work, so the account is kept.
    if (m_state == SignInState::Succeeded && (m_page == Page::SignIn || m_page == Page::Password) && m_account &&
        result == QDialog::Rejected)
        result = QDialog::Accepted;
    m_closeTimer.stop();
    m_expiry.stop();
    abortFlows();
    clearPendingPassword();
    QDialog::done(result);
}

void AddAccountDialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    Paint::paintBackground(p, rect(), Theme::current().bg);
}

void AddAccountDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    m_stack->syncHeight();
}

void AddAccountDialog::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    if (m_page == Page::Choose && event->modifiers() == Qt::NoModifier && (key == Qt::Key_Down || key == Qt::Key_Up)) {
        moveCardFocus(key == Qt::Key_Down ? 1 : -1);
        event->accept();
        return;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !qobject_cast<QAbstractButton*>(focusWidget())) {
        // Enter in a field submits its page. (Not through QDialog's default button: the last button made default wins, and that is
        // one of the hidden pages' buttons as often as not.)
        if (m_page == Page::Password) {
            submitPassword();
            event->accept();
            return;
        }
        if (m_page == Page::Offline) {
            addOffline();
            event->accept();
            return;
        }
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        // Enter presses the button that has focus (QDialog would press the default one instead, e.g. for the back arrow).
        if (auto* button = qobject_cast<QAbstractButton*>(focusWidget()); button && !qobject_cast<QPushButton*>(button)) {
            button->click();
            event->accept();
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

}  // namespace Accounts
}  // namespace Zero
