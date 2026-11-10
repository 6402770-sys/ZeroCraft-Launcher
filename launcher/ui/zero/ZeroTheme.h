// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the ZERO application themes (ITheme implementations): "ZERO Obsidian" and "Minecraft".
#pragma once

#include "ui/themes/FusionTheme.h"
#include "ui/zero/ZeroTokens.h"

namespace Zero {

/// Called by ThemeManager when the application theme changes. The popup / dark title bar hooks only act while
/// a ZERO theme is active, so they stay inert after the user switches to another theme.
void setThemeActive(bool active);
bool isThemeActive();

/// True for the ids of every built-in ZERO theme ("zero-obsidian", "zero-minecraft", ...).
bool isZeroThemeId(const QString& id);

/// Applies a ZERO ThemeSpec app-wide: Fusion base style, palette, base font and the generated stylesheet.
/// The spec comes from baseSpec() (Obsidian here); the "ZeroReduceMotion" setting (when an Application exists) is
/// carried into it.
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

   protected:
    /// The ThemeSpec this theme applies (before user settings such as reduced motion are merged in).
    virtual ThemeSpec baseSpec();
};

/// "Minecraft Theme": overworld stone, grass and diamond; notched corners, bevels, stone texture, pixel titles.
class ZeroMinecraftTheme : public ZeroTheme {
   public:
    static constexpr const char* kId = "zero-minecraft";

    ~ZeroMinecraftTheme() override = default;

    QString id() override;
    QString name() override;
    QString tooltip() override;

   protected:
    ThemeSpec baseSpec() override;
};

}  // namespace Zero
