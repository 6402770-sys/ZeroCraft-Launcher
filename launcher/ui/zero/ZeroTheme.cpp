// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroTheme.h"

#include <QApplication>
#include <QEvent>
#include <QAbstractSpinBox>
#include <QDebug>
#include <QDialog>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMenu>
#include <QObject>
#include <QStyleHints>
#include <QVariantAnimation>
#include <QWidget>
#include <atomic>
#include "Application.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroRipple.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace Zero {

namespace {

/// True while zero-obsidian is the active application theme (see setThemeActive()). The hooks below do nothing otherwise.
std::atomic<bool> g_themeActive{ false };

/// Immersive dark mode for the native title bar (Windows 10 20H1+ / 11). No-op on other platforms.
void applyTitleBarMode(QWidget* window, bool dark)
{
#ifdef Q_OS_WIN
    if (!window || !window->isWindow())
        return;
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL value = dark ? TRUE : FALSE;
    constexpr DWORD kImmersiveDarkMode = 20;     // DWMWA_USE_IMMERSIVE_DARK_MODE
    constexpr DWORD kImmersiveDarkModeOld = 19;  // pre-20H1 builds
    HRESULT hr = DwmSetWindowAttribute(hwnd, kImmersiveDarkMode, &value, sizeof(value));
    if (FAILED(hr))
        hr = DwmSetWindowAttribute(hwnd, kImmersiveDarkModeOld, &value, sizeof(value));
    qDebug() << "[Zero] immersive dark title bar" << (dark ? "on" : "off") << (SUCCEEDED(hr) ? "applied to" : "FAILED for")
             << window->metaObject()->className();
#else
    Q_UNUSED(window);
    Q_UNUSED(dark);
#endif
}

bool isFramedWindow(const QWidget* w)
{
    return w->isWindow() && (w->windowType() == Qt::Window || w->windowType() == Qt::Dialog);
}

/// Dialogs fade in through the window opacity (160 ms). The opacity is set to 0 while the dialog is polished (which
/// happens before its native window is shown, so it never flashes at full opacity), then eased to 1 on Show.
bool isFadingDialog(const QWidget* w)
{
    return qobject_cast<const QDialog*>(w) && isFramedWindow(w);
}

void fadeDialogIn(QWidget* dialog)
{
    if (!Motion::enabled()) {
        dialog->setWindowOpacity(1.0);
        return;
    }
    auto* anim = new QVariantAnimation(dialog);
    anim->setDuration(Tokens::durFast + 20);  // 160 ms
    anim->setStartValue(dialog->windowOpacity());
    anim->setEndValue(1.0);
    anim->setEasingCurve(Motion::easeEnter());
    QObject::connect(anim, &QVariantAnimation::valueChanged, dialog, [dialog](const QVariant& v) { dialog->setWindowOpacity(v.toReal()); });
    QObject::connect(anim, &QVariantAnimation::finished, dialog, [dialog]() { dialog->setWindowOpacity(1.0); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

/// Qt style sheets cannot round the corners of top-level popup windows. Menus, tooltips and combo popups are
/// made translucent + frameless so the border-radius from obsidian.qss shows through. The same filter gives every
/// top-level window the dark title bar. Both only apply while the ZERO theme is active.
class PopupFilter : public QObject {
   public:
    using QObject::QObject;

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!watched->isWidgetType())
            return false;
        auto* w = static_cast<QWidget*>(watched);
        if (!g_themeActive.load()) {
            // A dialog polished (opacity 0) under a ZERO theme but shown after switching to another theme must not stay invisible.
            if (event->type() == QEvent::Show && isFadingDialog(w) && w->windowOpacity() < 1.0)
                w->setWindowOpacity(1.0);
            return false;
        }
        if (event->type() == QEvent::Show) {
            if (isFramedWindow(w))
                applyTitleBarMode(w, true);
            if (isFadingDialog(w) && w->windowOpacity() < 1.0)
                fadeDialogIn(w);
            return false;
        }
        if (event->type() == QEvent::Polish) {
            if (auto* spin = qobject_cast<QAbstractSpinBox*>(w)) {
                // Same 10 px text inset as QLineEdit (style sheets cannot reach the embedded editor, see obsidian.qss).
                if (auto* editor = spin->findChild<QLineEdit*>(QString(), Qt::FindDirectChildrenOnly))
                    editor->setTextMargins(Tokens::spaceMd - 1, 0, 0, 0);
                return false;
            }
        }
        if (event->type() != QEvent::Polish || !w->isWindow())
            return false;
        if (isFadingDialog(w) && Motion::enabled())
            w->setWindowOpacity(0.0);  // see fadeDialogIn()
        if (qobject_cast<QMenu*>(w) || w->inherits("QTipLabel") || w->inherits("QComboBoxPrivateContainer")) {
            w->setWindowFlag(Qt::FramelessWindowHint, true);
            w->setWindowFlag(Qt::NoDropShadowWindowHint, true);
            w->setAttribute(Qt::WA_TranslucentBackground, true);
        }
        return false;
    }
};

/// Motion on/off from the ZeroMotion setting (+ legacy ZeroReduceMotion + the Windows animation flag).
bool motionEnabledFromSettings()
{
    if (auto* app = qobject_cast<Application*>(qApp)) {
        return Motion::resolveEnabled(app->settings()->get("ZeroMotion").toString(), app->settings()->get("ZeroReduceMotion").toBool(),
                                      Motion::systemAnimationsEnabled());
    }
    return true;
}

void refreshMotion()
{
    Theme::setReduceMotion(!motionEnabledFromSettings());
}

void installHooks()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    qApp->installEventFilter(new PopupFilter(qApp));
    Ripple::install();

    // Pixel/UI fonts follow the active spec for every widget that carries a zeroRole.
    QObject::connect(&Theme::instance(), &Theme::changed, qApp, []() { Style::refreshRoleFonts(); });

    // Native menu / tooltip / combo popup animations follow the motion switch while a ZERO theme is active.
    QObject::connect(&Theme::instance(), &Theme::changed, qApp, []() {
        if (g_themeActive.load())
            Motion::syncPopupEffects(Motion::enabled());
    });
    Motion::syncPopupEffects(Motion::enabled());

    if (auto* app = qobject_cast<Application*>(qApp)) {
        for (const char* key : { "ZeroMotion", "ZeroReduceMotion" }) {
            if (auto setting = app->settings()->getSetting(key))
                QObject::connect(setting.get(), &Setting::SettingChanged, qApp, [](const Setting&, const QVariant&) { refreshMotion(); });
        }
        // The Windows "Animation effects" switch has no change notification we rely on: re-read it when the app is
        // activated (the user comes back from the Settings app).
        QObject::connect(qApp, &QGuiApplication::applicationStateChanged, qApp, [](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive)
                refreshMotion();
        });
    }
}

}  // namespace

void setThemeActive(bool active)
{
    if (g_themeActive.exchange(active) == active)
        return;
    // Windows that are already open follow the theme switch: ZERO = dark title bar, otherwise back to the system setting.
    const bool systemDark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (isFramedWindow(w) && w->testAttribute(Qt::WA_WState_Created))
            applyTitleBarMode(w, active || systemDark);
    }
}

bool isThemeActive()
{
    return g_themeActive.load();
}

bool isZeroThemeId(const QString& id)
{
    return id.startsWith(QLatin1String("zero-"));
}

bool specForThemeId(const QString& id, ThemeSpec* out)
{
    if (id == QLatin1String(ZeroTheme::kId))
        *out = ThemeSpec::obsidian();
    else if (id == QLatin1String(ZeroMinecraftTheme::kId))
        *out = ThemeSpec::minecraft();
    else
        return false;
    return true;
}

ThemeSpec ZeroTheme::baseSpec()
{
    return ThemeSpec::obsidian();
}

void ZeroTheme::apply(bool initial)
{
    ThemeSpec spec = baseSpec();
    spec.reduceMotion = !motionEnabledFromSettings();
    Theme::setCurrent(spec);

    ITheme::apply(initial);  // Fusion base + palette + stylesheet
    QApplication::setFont(Style::font(Role::Body));
    installHooks();
    // Replacing the style sheet makes Qt restore the fonts widgets had before it was applied, which undoes the
    // refresh done on Theme::changed. Re-apply the pixel/UI role fonts now that the new sheet is in place.
    Style::refreshRoleFonts();
}

QString ZeroTheme::id()
{
    return QString::fromLatin1(kId);
}

QString ZeroTheme::name()
{
    return QObject::tr("ZERO Obsidian");
}

QString ZeroTheme::tooltip()
{
    return QObject::tr("The default ZERO look: monochrome and calm.");
}

bool ZeroTheme::hasStyleSheet()
{
    return true;
}

QString ZeroTheme::appStyleSheet()
{
    return Style::styleSheet(Theme::current());
}

QPalette ZeroTheme::colorScheme()
{
    return Style::palette(Theme::current());
}

ThemeSpec ZeroMinecraftTheme::baseSpec()
{
    return ThemeSpec::minecraft();
}

QString ZeroMinecraftTheme::id()
{
    return QString::fromLatin1(kId);
}

QString ZeroMinecraftTheme::name()
{
    return QObject::tr("Minecraft");
}

QString ZeroMinecraftTheme::tooltip()
{
    return QObject::tr("Overworld stone, grass and diamond. Pixel details, readable text.");
}

double ZeroTheme::fadeAmount()
{
    return 0.5;
}

QColor ZeroTheme::fadeColor()
{
    return Theme::current().bg;
}

}  // namespace Zero
