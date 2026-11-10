// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroTheme.h"

#include <QApplication>
#include <QEvent>
#include <QAbstractSpinBox>
#include <QDebug>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMenu>
#include <QObject>
#include <QStyleHints>
#include <QWidget>
#include <atomic>
#include "Application.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"
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

/// Qt style sheets cannot round the corners of top-level popup windows. Menus, tooltips and combo popups are
/// made translucent + frameless so the border-radius from obsidian.qss shows through. The same filter gives every
/// top-level window the dark title bar. Both only apply while the ZERO theme is active.
class PopupFilter : public QObject {
   public:
    using QObject::QObject;

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!g_themeActive.load() || !watched->isWidgetType())
            return false;
        auto* w = static_cast<QWidget*>(watched);
        if (event->type() == QEvent::Show) {
            if (isFramedWindow(w))
                applyTitleBarMode(w, true);
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
        if (qobject_cast<QMenu*>(w) || w->inherits("QTipLabel") || w->inherits("QComboBoxPrivateContainer")) {
            w->setWindowFlag(Qt::FramelessWindowHint, true);
            w->setWindowFlag(Qt::NoDropShadowWindowHint, true);
            w->setAttribute(Qt::WA_TranslucentBackground, true);
        }
        return false;
    }
};

void installHooks()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    qApp->installEventFilter(new PopupFilter(qApp));

    if (auto* app = qobject_cast<Application*>(qApp)) {
        if (auto setting = app->settings()->getSetting("ZeroReduceMotion")) {
            QObject::connect(setting.get(), &Setting::SettingChanged, qApp,
                             [](const Setting&, const QVariant& value) { Theme::setReduceMotion(value.toBool()); });
        }
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

void ZeroTheme::apply(bool initial)
{
    ThemeSpec spec = ThemeSpec::obsidian();
    if (auto* app = qobject_cast<Application*>(qApp))
        spec.reduceMotion = app->settings()->get("ZeroReduceMotion").toBool();
    Theme::setCurrent(spec);

    ITheme::apply(initial);  // Fusion base + palette + stylesheet
    QApplication::setFont(Style::font(Role::Body));
    installHooks();
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

double ZeroTheme::fadeAmount()
{
    return 0.5;
}

QColor ZeroTheme::fadeColor()
{
    return Theme::current().bg;
}

}  // namespace Zero
