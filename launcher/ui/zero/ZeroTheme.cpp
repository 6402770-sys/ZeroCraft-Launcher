// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroTheme.h"

#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QObject>
#include <QWidget>
#include "Application.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

namespace {

/// Qt style sheets cannot round the corners of top-level popup windows. Menus, tooltips and combo popups are
/// made translucent + frameless so the border-radius from obsidian.qss shows through.
class PopupFilter : public QObject {
   public:
    using QObject::QObject;

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::Polish || !watched->isWidgetType())
            return false;
        auto* w = static_cast<QWidget*>(watched);
        if (!w->isWindow())
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
