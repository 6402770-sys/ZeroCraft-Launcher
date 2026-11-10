# ZERO Shell — Home & Library spec

Binding design for the main window. Tokens/components: `docs/zero/DESIGN.md`.

## Architecture
- `MainWindow` stays the **controller**: it owns every `QAction`, the `InstanceView` + `InstanceProxyModel`,
  selection, launch/kill, dialogs. Its view layer is replaced: central widget = `ZeroShell`.
- `ZeroShell` (`launcher/ui/zero/shell/`): `[ZeroNavRail | QStackedWidget]`. API: `addPage(id, icon, label, QWidget*)`,
  `setCurrentPage(id)`, signal `pageChanged(id)`. Page enter = `Transition::Rise` snapshot overlay, 200 ms (instant with motion off).
- Pages live in `launcher/ui/zero/pages/`. They never duplicate launcher logic: they trigger MainWindow's existing
  actions or call the same APIs MainWindow calls (e.g. `APPLICATION->launch(...)`).
- Prism's `mainToolBar`, `newsToolBar`, `instanceToolBar`, menu bar, status bar and cat background are no longer shown.
  Their actions remain (shortcuts, context menus and pages use them).
- Windows title bar uses immersive dark mode (DWM attribute 20) for all top-level windows.

## Window
Default 1280×800, minimum 1100×700. Rail 220 px (collapses to 64 px below 1200 px window width).

## Nav rail (top → bottom)
1. Wordmark "ZERO" (display weight 600, letter-spacing 3px) + caption "Launcher", padding xl.
2. Items: **Home** (`house`), **Library** (`library-big`), **Tools** (`wrench`), **Appearance** (`palette`). Later tasks add Discover, Worlds, Downloads, Theme Studio.
3. Stretch.
4. **Account chip**: 28 px avatar (account skin face if available, else `user` icon on elevated circle) + profile name
   (label) + caption "Microsoft account"/"Offline"/"No account". Click → Prism's existing accounts menu (switch / manage).
5. **More** (`ellipsis`, ghost): menu with Folders ▸ (existing folders menu), Help ▸ (existing help actions that are enabled),
   About, Check for updates (only if the updater is enabled).
6. **Settings** (`settings`): opens the existing global settings dialog (not a page yet).

## Home page
Content max width 1280, page padding xl, vertical gap xxl between sections.
1. **Hero** (`ZeroCard`, min height 200): instance to continue = most recently launched instance
   (`BaseInstance::lastLaunch()`), else the first instance by name.
   - Left: 96 px instance icon on an elevated 128 px rounded tile (radius lg).
   - Overline "CONTINUE PLAYING", name (display), caption meta: `<version · loader> · Played <relative time> · <total playtime>`.
   - Buttons: **Play** (primary, lg, `play` icon) → launches that instance; while running → **Stop** (danger, `square`)
     + "Running" badge (success dot). **Edit** (secondary) → instance edit window. **Folder** (ghost, `folder-open`).
2. **Recently played** (heading + "View library" ghost link at right): up to 6 small instance cards, sorted by last launch,
   only instances launched at least once. Section hidden if none. Card click selects it in Library and switches page;
   card Play button launches.
3. **Shortcuts** grid (4 tiles, `ZeroCard` clickable, icon + label + caption): New instance (`plus`), Import (`package`),
   Accounts (`user`), Instances folder (`folder-open`).
- Empty library → whole page is a `ZeroEmptyState`: icon `library-big`, "Your library is empty",
  "Create an instance, install a modpack or import one you already have.", primary "New instance", secondary "Import".

## Library page
- `ZeroPageHeader` "Library", subtitle "<N> instances". Actions (right): search field (240 px, `search` icon,
  placeholder "Search instances", Ctrl+F focuses, Esc clears), sort combo (Name / Last played), view toggle
  (grid `layout-grid` / list `list`, ghost checkable pair), **New instance** (primary, `plus`).
- Body: `[collection | details panel 300 px]`, gap lg.
- **Grid**: Prism's `InstanceView` (keeps groups, drag-and-drop, keyboard nav, inline rename, context menu) with a new
  `ZeroInstanceDelegate` painting cards: 168×188 cell, card radius lg, bg surface, border border; icon 64 px centered
  in the top 112 px area (bg elevated, top corners rounded); name (label 13/600, elided, 2 lines max) + caption
  (version · loader). Hover: borderStrong + surfaceHover. Selected: border accent 1px + bg elevated. Running: success dot top-right.
  Group headers: overline style + count, collapsible as before.
- **List**: `QTreeView` (no root decoration, uniform rows 36 px) over the same instances: Name (icon 20 px + name),
  Version, Loader, Group, Last played (relative), Playtime. Sortable by header. Same selection as grid (one selection model source of truth via MainWindow).
- Search filters both views by name, group and version (case-insensitive substring).
- **Details panel** (hidden when nothing selected → small empty state "Select an instance"): 96 px icon (click = change icon),
  name (title, double-click = rename), caption meta, stats rows (Last played, Total playtime, Group), **Play/Stop**
  (primary, full width, lg). Then a ghost action list with icons: Edit (`pencil`), Open folder (`folder-open`),
  Change group (`list-filter`), Copy (`copy`), Export ▸ (Zip / Modrinth / CurseForge), Create shortcut (`terminal`),
  Delete (danger, `trash-2`). All mapped to the existing MainWindow actions; disabled states mirror them.

## Play button (`ZeroPlayButton`)
One painted 40 px button (Home hero, Library details panel; the small play icons on "Recently played" cards stay plain), bound to an
instance id and driven by `Application::launchPhaseChanged(id, Idle|Preparing|Running)` / `launchEnded(id, success, aborted,
gameStarted)`, so it is right however the launch started. `Preparing` = launch controller created (`Application::launch`),
`Running` = game process up (`LauncherPartLaunch`, `LoggedProcess::Running`), `Idle` = `controllerFinished`.
- **Idle**: accent fill, `play` icon + "Play"; hover -> `accentHover` (140 ms) and the icon nudges 2 px right; press scales the content to 0.97
  (Minecraft: block press, content drops 2 px, bevel collapses); release springs back (OutBack).
- **Preparing**: "Launching...", `accentPressed` fill, clicks ignored. Activity sweep (white 18 % band, 40 % width, 1100 ms per pass; Minecraft:
  12-segment XP bar along the bottom edge lighting up in `accentHover`). The sweep runs even with motion off and stops when the state changes.
- **Running**: the sweep finishes, then 280 ms crossfade to `elevated` + 1 px `border`, success dot + "Running". Hover morphs to a danger `square` "Stop";
  click emits `stopRequested` (MainWindow's kill flow).
- **Failed** (launch ended, not aborted, game never ran): "Launch failed" in danger for 2.5 s with a +-4 px content shake (320 ms, none with motion off), then
  Idle. Aborted by the user -> Idle silently. A crash after the game started is not a launch failure (Crash Detective toast handles it).
- Accessible name + tooltip per state; Space / Enter activate; keyboard focus draws a 2 px `accent2` ring.

## Appearance page (`AppearancePage`)
Rail item "Appearance" (`palette`). Max width 960, scrolls when short.
- **Theme**: `ThemePreview` (full width, 16:9, max 340 px) paints a miniature ZERO window from a `ThemeSpec` (never the active one: textured background,
  rail + active pill, header in the spec's title font, hero card with primary button bevelled/notched per spec, two cards). One card per ZERO theme
  (`ZeroTheme.h: specForThemeId`): swatches (bg, surface, accent, accent2), name, tooltip, "Current" badge on the applied one. Selecting a card
  crossfades the preview (200 ms) and applies nothing; **Apply theme** (enabled when selection != applied) emits `applyThemeRequested`, MainWindow applies
  through `ThemeManager::setApplicationTheme` and persists `ApplicationTheme` (the global theme transition plays). The page follows theme changes made elsewhere.
- **Motion**: segmented "Follow Windows" / "On" / "Off" bound to the `ZeroMotion` setting (live). Caption states what the choice means; for Follow Windows it says whether
  Windows animation effects are on or off. "Preview" replays a Rise transition on the preview (nothing with motion off).
- "More appearance options" opens Prism's global settings on `appearance-settings`.

## Tools page and Crash Detective
- `ToolsPage` (`pages/ToolsPage`): header "Tools" + grid of tool cards (only Crash Detective for now). A card opens the tool inside the
  page; the tool has a back button ("Tools").
- `CrashDetectiveView`: instance combo (defaults to the Library selection), **Analyze**, spinner while the work runs on a
  `QtConcurrent` thread, then one `FindingCard` per finding (severity icon, title, confidence badge, explanation, "What to try",
  action buttons, collapsible monospace evidence with source + line). Caption lists the analyzed files and their timestamps.
  No findings -> "No known cause recognised" (Open log / Open logs folder); no log files -> "No logs found".
- Badge policy: only a *Confirmed* cause is tinted (danger for Critical, warning for Warning); Likely/Possible are neutral. Never green.
- Engine: `launcher/zero/diagnostics/` (`CrashDetective`, `CrashReportCollector`); tests `tests/CrashDetective_test.cpp`.
- Crash hook: `LauncherPartLaunch::on_state` emits `Application::gameStoppedUnexpectedly(instanceId, exitCode)` when the game process
  crashes or exits non-zero (a user kill is `Aborted` and does not fire). `MainWindow` shows `ZeroToast` "<name> stopped
  unexpectedly" with action **Diagnose** -> Tools > Crash Detective for that instance, analysis started with the exit code.
- Instance settings action: `APPLICATION->showInstanceWindow(inst, "settings")` (the Edit window, Settings page).

## Dev aids
- `--zero-screenshot <file.png>` (+ optional `--zero-page <home|library|tools|appearance>`): show main window, wait for layout, `grab()` to PNG,
  quit. Skips the setup wizard. Always combine with `-d <temp dir>` (never the real data dir).
- Screenshot-mode environment variables (dev only, read by `MainWindow::zeroScreenshot`): `ZERO_SCREENSHOT_SIZE=<w>x<h>`
  (default 1280x800), `ZERO_SCREENSHOT_THEME=<theme id>` (`zero-obsidian` | `zero-minecraft`: applies that theme through
  `ThemeManager::setApplicationTheme` before grabbing), `ZERO_SCREENSHOT_RUNNING=<instance id>` (flips that instance to running through the normal signal
  path), `ZERO_SCREENSHOT_SEARCH=<text>` (types into the Library search). List mode is the `ZeroLibraryView=list` setting.
  `ZERO_SCREENSHOT_DIAGNOSE=<instance id>` opens Tools > Crash Detective for that instance and analyzes it
  (`ZERO_SCREENSHOT_DIAGNOSE_EXIT=<code>` passes an exit code, `ZERO_SCREENSHOT_EVIDENCE=1` expands every evidence block).
- `ZERO_SCREENSHOT_LAUNCH_PHASE=<instance id>:<preparing|running>` emits `Application::launchPhaseChanged` for that instance 300 ms after start (the signal the Play
  buttons listen to), `ZERO_SCREENSHOT_SELECT_THEME=<theme id>` selects a card on the Appearance page (nothing applied), `ZERO_SCREENSHOT_TOAST=1` shows one toast of each kind.
- Motion evidence frames (after the normal grab): `ZERO_SCREENSHOT_TRANSITION=<page id>` (`home` | `library` | `tools`)
  switches to that page and `ZERO_SCREENSHOT_THEME_TO=<theme id>` switches theme, each saving the window grab every 40 ms
  for 320 ms as `<file>-t0.png` .. `<file>-t8.png`, plus `<file>-tfinal.png` once everything settled (the last frame must
  equal the settled UI). With both set the page switch runs first, then the theme switch (files `<file>-theme-t*.png`).
  Needs motion on (default `system`; set the `ZeroMotion` setting to `on` when Windows "Animation effects" is off).
