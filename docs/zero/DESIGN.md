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
