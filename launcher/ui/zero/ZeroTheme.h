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

/// True for the ids of every ZERO theme: the built-ins ("zero-obsidian", "zero-minecraft") and the user's custom ones
/// ("zero-custom-<slug>").
bool isZeroThemeId(const QString& id);

/// The ThemeSpec behind a ZERO theme id (before user settings such as motion are merged in). Custom ids are answered from
/// the themes ThemeManager has loaded. False for ids that are not (loaded) ZERO themes. Used by the Appearance page to
/// preview a theme without applying it.
bool specForThemeId(const QString& id, ThemeSpec* out);

/// Applies a ZERO ThemeSpec app-wide: Fusion base style, palette, base font and the generated stylesheet.
/// The spec comes from baseSpec() (Obsidian here); the motion setting ("ZeroMotion", legacy "ZeroReduceMotion"; when an
/// Application exists) is carried into it.
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

/// Applies `spec` app-wide exactly like ZeroTheme::apply does for a built-in theme (spec -> Theme::setCurrent, Fusion base style,
/// palette, generated stylesheet, role fonts), without registering anything with ThemeManager and without a transition. The
/// motion setting is merged into the spec. For live previews (the theme editor); the caller restores the real theme afterwards.
void applySpecLive(const ThemeSpec& spec);

/// The ZERO default theme: Minecraft (first start, empty setting, first card on the Appearance page).
inline constexpr const char* kDefaultThemeId = ZeroMinecraftTheme::kId;

/// The app mark file a theme shows: its own icon when it has one (and the file exists), else the mark of its base theme (the pixel-art
/// mark for a Minecraft one).
QString logoPathFor(const ThemeSpec& spec);
/// Path of the app mark that matches the active theme: logoPathFor(the current spec) while a ZERO theme is active, else the plain mark.
QString appLogoPath();
/// The app mark of the active theme (window / taskbar icon, rail header).
QIcon appLogo();

}  // namespace Zero
