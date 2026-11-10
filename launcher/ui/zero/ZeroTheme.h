// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the "ZERO Obsidian" application theme (ITheme implementation).
#pragma once

#include "ui/themes/FusionTheme.h"

namespace Zero {

/// Called by ThemeManager when the application theme changes. The popup / dark title bar hooks only act while
/// the ZERO theme is active, so they stay inert after the user switches to another theme.
void setThemeActive(bool active);
bool isThemeActive();

/// Applies the Obsidian ThemeSpec app-wide: Fusion base style, palette, base font and the generated stylesheet.
/// Reads the "ZeroReduceMotion" setting (when an Application exists) into the active ThemeSpec.
class ZeroTheme : public FusionTheme {
   public:
    static constexpr const char* kId = "zero-obsidian";

    ~ZeroTheme() override = default;

    void apply(bool initial) override;
    QString id() override;
    QString name() override;
    QString tooltip() override;
    bool hasStyleSheet() override;
    QString appStyleSheet() override;
    QPalette colorScheme() override;
    double fadeAmount() override;
    QColor fadeColor() override;
};

}  // namespace Zero
