# ZERO Design System — Obsidian

Source of truth for every ZERO UI. Tokens live in code at `launcher/ui/zero/ZeroTokens.h`;
the app-wide stylesheet is generated from those tokens by `ZeroStyle` (never hand-copy hex values into widgets).

## Principles
Premium desktop app, not a gaming skin. Monochrome, deliberate, dense but calm.
No gradients, no glow, no saturated color except muted status colors. Hierarchy comes from
surface elevation, weight and spacing, not from color.

## Color tokens
| Token | Hex | Use |
|---|---|---|
| `bg` | `#101010` | window / page background |
| `surface` | `#191919` | nav rail, cards, inputs, menus |
| `surfaceHover` | `#1F1F1F` | hover on surface items |
| `elevated` | `#222222` | active nav item, secondary buttons, popovers, tooltips |
| `higher` | `#292929` | hover on elevated, selected rows |
| `pressed` | `#2E2E2E` | pressed state |
| `border` | `#303030` | 1px borders, separators |
| `borderStrong` | `#3A3A3A` | hovered card/input border |
| `accent` | `#D4D4D4` | primary buttons, focus ring, progress fill, active indicator |
| `accentHover` | `#E8E8E8` | |
| `accentPressed` | `#BDBDBD` | |
| `onAccent` | `#101010` | text/icons on accent |
| `text` | `#F5F5F5` | primary text |
| `textSecondary` | `#929292` | secondary text, inactive nav |
| `textTertiary` | `#6B6B6B` | placeholders, disabled, empty-state icons |
| `success` | `#7BC59A` | muted status only (dots, small badges, toast icon) |
| `warning` | `#D9B36C` | |
| `danger` | `#E07A7A` | destructive text/icons |
| `info` | `#8AB4D9` | |

Status colors never fill large areas; at most a 12% tint background behind a badge.

## Typography
Family: `Segoe UI Variable Text`, fallback `Segoe UI`, then Qt default. (Native on Win 10/11, no bundling.)

| Role | Size px | Weight | Color |
|---|---|---|---|
| display | 28 | 600 | text |
| title (page title) | 20 | 600 | text |
| heading (section/card title) | 15 | 600 | text |
| body | 13 | 400 | text |
| label (buttons, nav) | 13 | 500 | per component |
| caption | 11 | 400 | textSecondary |
| overline (section labels) | 10 | 600, uppercase, letter-spacing 0.8px | textTertiary |

## Spacing, radius, elevation, motion
- Spacing scale (px): `xs 4, sm 8, md 12, lg 16, xl 24, xxl 32, xxxl 48`. Page padding = `xl`. Gap between cards = `lg`.
- Radius: `sm 4` (tags, tooltips, scrollbar), `md 6` (buttons, inputs, nav items), `lg 10` (cards, menus, toasts), `xl 14` (dialogs, hero).
- Elevation: flat by default (1px `border`). Only popovers, toasts and dialogs get a shadow
  (`QGraphicsDropShadowEffect`, blur 24, offset 0,8, rgba(0,0,0,0.55)).
- Motion: hover 120 ms, page/opacity transitions 180 ms, easing OutCubic. All animation goes through
  `ZeroMotion` helpers that return instantly when the user setting `ZeroReduceMotion` is true.

## Components (all in `launcher/ui/zero/`)
- **Button** — any `QPushButton`/`QToolButton` with dynamic property `zeroVariant`:
  `primary` (bg accent, text onAccent, hover accentHover), `secondary` (bg elevated, 1px border, hover higher),
  `ghost` (transparent, textSecondary → text on hover, hover bg surfaceHover), `danger` (text danger, hover 12% danger tint).
  Sizes via `zeroSize`: `md` height 32 / padding 0 14, `lg` height 40 / padding 0 20 / weight 600 (Play). Radius md.
- **Input** — `QLineEdit`, `QComboBox`, `QSpinBox`: bg surface, 1px border, radius md, height 32, focus border accent, placeholder textTertiary.
- **Card** — `ZeroCard` (QFrame subclass, painted): bg surface, 1px border, radius lg; hover → bg surfaceHover + borderStrong (animated); optional `clickable`.
- **NavRail** — `ZeroNavRail`: width 220 (collapsed 64). Top: ZERO wordmark. Items 36 px, radius md, icon 18 px + label;
  inactive textSecondary; hover bg surface + text; active bg elevated + text + 2 px accent indicator at left edge.
  Bottom: account chip (avatar, name, menu) above Settings.
- **PageHeader** — title (title role), optional caption subtitle, right-aligned actions; bottom margin `xl`.
- **EmptyState** — centered: 40 px icon textTertiary, heading, body textSecondary (max 360 px wide), optional primary action.
- **Toast** — `ZeroToast`: bottom-right stack, bg elevated, border, radius lg, padding md, status icon, auto-dismiss 4 s, optional action.
- **Progress** — bar 4 px, track elevated, fill accent, radius 2. `ZeroSpinner`: arc, 16/24 px, accent, 1 s rotation.
- **Menus / context menus** — bg surface, border, radius lg, item padding 6×12, hover elevated, separators border.
- **Tooltip** — bg elevated, border, radius sm, text.
- **Scrollbars** — 8 px, transparent track, handle border color radius 4, hover borderStrong.
- **Tags/badges** — height 20, radius sm, bg elevated, caption text; status badges use 12% tint + status text.

## Icons
Shell and new pages use Lucide icons (ISC license, attribution in `docs/zero/THIRD_PARTY.md`), stored as SVG under
`launcher/resources/zero/icons/`, rendered through `ZeroIcons::get(name, color)` which tints to the requested token.
Legacy Prism pages keep Prism's icon themes; ZERO defaults the icon theme to `flat_white`.

## Layout rules
- Shell: `[NavRail | Page]`. Pages scroll vertically inside; the rail never scrolls.
- Content max width 1280 px on Home; Library uses full width.
- Never show a control that does nothing. Unfinished destinations are hidden, not stubbed.

---

# Design v2 (Mission 002, 2026-11-10) — binding

## Theme engine
- A theme is a `Zero::ThemeSpec` (`ZeroTokens.h`). Fields beyond colors: `id`, `name`, `accent2`, `highlight`,
  radius scale (`radiusSm/Md/Lg/Xl`), `corners` (`Rounded` | `Notched`), `bevel`, `texture`, `pixelRoles`
  (type roles drawn with the built-in "ZERO Pixel" font). Widgets read only `Zero::Theme::current()` and repaint on
  `Theme::changed()`. Painted panels use `Zero::Paint::panelPath(rect, radius)` so the corner style follows the theme.
- Each spec is an `ITheme` in Prism's ThemeManager (`zero-obsidian`, `zero-minecraft`), persisted by the existing
  `ApplicationTheme` setting, switchable live. Stylesheet = `obsidian.qss` (base, token placeholders) + optional
  per-theme overlay (`minecraft.qss`). Future custom themes = another spec (+ overlay).

## Minecraft Theme (`zero-minecraft`)
Overworld at night, carved in stone. Readable first; the personality lives in surfaces, edges and a few pixel details.

| Token | Hex | Meaning |
|---|---|---|
| bg | `#16171A` | deep deepslate |
| surface / surfaceHover | `#1F2125` / `#25282D` | dark stone |
| elevated / higher / pressed | `#2B2E33` / `#33373D` / `#3A3F46` | charcoal stone |
| border / borderStrong | `#393D44` / `#4A4F57` | |
| bevelDark | `#0D0E10` | block shadow edge (bottom/right of raised controls) |
| accent / hover / pressed / onAccent | `#67B84A` / `#78C95A` / `#4F9A35` / `#0E1A09` | grass |
| accent2 | `#4FD0D6` | diamond (focus ring, links, selection indicator) |
| highlight | `#E6B83E` | gold (favorites, featured) |
| text / textSecondary / textTertiary | `#EFEBE0` / `#A8A397` / `#757067` | warm white / stone greys |
| success / warning / danger / info | `#3CC77A` / `#E6B83E` / `#E25A4F` / `#5DB3E8` | emerald / gold / redstone / lapis |

- Shape: radius 2/2/3/4; painted panels use **notched corners** (a 2 px pixel step instead of a curve).
- **Bevel**: raised controls (buttons, cards, nav pill) have a 1 px light top edge (white 7%) and a 2–3 px `bevelDark`
  bottom edge; pressing a button collapses the bevel and moves the label down 2 px (block press).
- **Texture**: window and sidebar backgrounds carry a deterministic, procedurally generated stone tile (4 px "pixels",
  ±4/255 luminance noise). Never behind text-dense surfaces (cards, inputs, menus stay flat).
- **ZERO Pixel** font: original 5×7 bitmap glyphs (printable ASCII) compiled into a TrueType font at runtime
  (`ZeroPixelFont`), license-clean. Used only for the wordmark, page titles and overlines. Body text stays Segoe UI.
- Pixel details: XP-style segmented progress on the Play button while launching, gem-shaped status marks in toasts.
  No creatures, no block textures copied from the game, no proprietary assets.

## Motion language
Purposeful, fast, never blocking input. All motion goes through `Zero::Motion`.

| Token | ms | Use |
|---|---|---|
| durPress | 90 | press feedback |
| durFast | 140 | hover, toggles, chips |
| durBase | 200 | page enter, panels, indicator glide |
| durSlow | 280 | dialogs, palette, toasts |
| durTheme | 360 | theme transition (Minecraft pixel dissolve 420) |

Easing: enter `OutCubic`, exit `InCubic`, glide/emphasis `OutQuint`, release `OutBack` (overshoot ≤ 1.3).
- Setting `ZeroMotion` = `system` (default, follows Windows "Animation effects") | `on` | `off`. Off = every transition
  is instant; activity indicators (spinner, launch sweep) still run because they carry information.
- Transitions use snapshot overlays (`QWidget::grab()` painted over the live UI, mouse-transparent), so the real
  widgets are interactive immediately and layouts never animate. Effects are removed when a transition ends.
- No looping animation unless something is actually in progress.

Signature motions: nav indicator glide · page enter (fade + 10 px rise) · card lift on hover (−2 px, shadow grows) ·
press ripple on ZERO buttons (Obsidian) / block press (Minecraft) · Play → Launching sweep → Running ·
theme crossfade (Obsidian) / pixel dissolve (Minecraft) · toast slide-in, stack glide, slide-out ·
command palette rise · grid/list crossfade.

Implementation notes (motion system, `ZeroMotion` / `ZeroTransition` / `ZeroRipple`):
- `ZeroMotion` setting is stored as `""` (never chosen) | `system` | `on` | `off`; `""` behaves like `system`, except that a
  legacy `ZeroReduceMotion=true` keeps meaning `off`. Resolution is the pure `Motion::resolveEnabled()`; the Windows flag is
  re-read when the setting changes and when the app is activated.
- Rise: the old snapshot is the opaque base of the overlay (it has to hide the live page) and the new picture, taken on the
  first animation tick so pending layouts have run, rises 10 px and fades in over it. That is what fades the old content out,
  instead of a separate 120 ms fade-out (which would let the live page show through).
- `ZeroCard` hover: a widget cannot paint outside its rect, so the "shadow" gathers along the card's inner bottom edge
  (grid cards in the Library do lift 2 px with a real outer shadow because the delegate owns the whole viewport).
- Dialogs fade in through the window opacity (160 ms): opacity 0 is set while the dialog polishes (before its native window
  exists) and eased to 1 on show.
