// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - stylesheet / palette / font generation from a ThemeSpec.
#pragma once

#include <QFont>
#include <QHash>
#include <QPalette>
#include <QString>
#include <QVariant>
#include "ZeroTokens.h"

class QWidget;

namespace Zero {
namespace Style {

/// The raw qss template (":/zero/obsidian.qss") with `@token@` placeholders.
QString templateSource();

/// Template with every placeholder substituted for `spec`. An unknown placeholder logs a qWarning and is left in
/// place (so the validation test fails). Image placeholders (`@img:<lucide>:<colorToken>@`) write a tinted SVG
/// into a temp directory and expand to a url(); they need a writable temp dir.
QString styleSheet(const ThemeSpec& spec);
QString styleSheet(const ThemeSpec& spec, const QString& templateText);

/// Every `@name@` -> value used by the template (colors as #rrggbb, lengths with px, status tints as rgba()).
QHash<QString, QString> tokenMap(const ThemeSpec& spec);

/// Color token by name ("bg", "textSecondary", ...); invalid QColor if unknown.
QColor colorByName(const ThemeSpec& spec, const QString& name);

/// App palette: Window=bg, Base=surface, Text=text, Button=elevated, Highlight=higher, PlaceholderText=textTertiary, ...
QPalette palette(const ThemeSpec& spec);

/// Font for a type role (Segoe UI Variable Text -> Segoe UI -> Qt default).
QFont font(Role role);

/// Name used by the `zeroRole` dynamic property ("title", "caption", ...).
QString roleName(Role role);

/// Sets `zeroRole` and the full role font on a widget (needed for overline letter-spacing/uppercase, which qss
/// cannot express) and re-polishes it.
void applyRole(QWidget* widget, Role role);

/// Sets a dynamic property and re-polishes so the stylesheet picks it up.
void setProperty(QWidget* widget, const char* name, const QVariant& value);

}  // namespace Style
}  // namespace Zero
