# ZERO Shell (v2) - Home, Library and the main window chrome

Binding design for the main window. Tokens/components: `docs/zero/DESIGN.md`.

## Architecture
- `MainWindow` stays the **controller**: it owns every `QAction`, the `InstanceView` + `InstanceProxyModel`,
  selection, launch/kill, dialogs. Its view layer is replaced: central widget = `ZeroShell`.
- `ZeroShell` (`launcher/ui/zero/shell/`): `[ZeroNavRail | column(ZeroTopBar, QStackedWidget)]`. API:
  `addPage(id, icon, label, ShellPage*, Placement::Top|Bottom)`, `setCurrentPage(id)`, `finishRail()` (More + Settings),
  `setAccount(...)`, `topBar()`, signal `pageChanged(id)`. Page enter = `Transition::Rise` snapshot overlay on the stack,
  200 ms (instant with motion off). The shell paints the stone texture; the top bar and pages are transparent over it.
- Every page derives from `Zero::ShellPage` (`shell/ShellPage.h`), the **page chrome API**: virtual `title()`,
  `subtitle()`, `breadcrumbParent()` (empty = none), `breadcrumbActivated()` (the parent segment was clicked), `topBarActions()`
  (a widget owned by the page; `setTopBarActions(w)` sets the default one) and the signal `chromeChanged()`. The top bar shows
  only the current page's chrome and re-reads it on `chromeChanged()`. It borrows the actions widget (reparents it) and
  gives it back, hidden, when another page becomes current.
- Pages live in `launcher/ui/zero/pages/`. They never duplicate launcher logic: they trigger MainWindow's existing
  actions or call the same APIs MainWindow calls (e.g. `APPLICATION->launch(...)`).
- Pages have **no header of their own**: title, subtitle, breadcrumb and the page's primary actions live in the top bar.
  (`ZeroPageHeader` stays in the design system for the component gallery.)
- Prism's `mainToolBar`, `newsToolBar`, `instanceToolBar`, menu bar, status bar and cat background are no longer shown.
  Their actions remain (shortcuts, context menus and pages use them).
- Windows title bar uses immersive dark mode (DWM attribute 20) for all top-level windows.

## Window
Default 1280x800, minimum 1100x700. Rail 220 px (collapses to 64 px below 1200 px window width). Top bar 60 px.
The rail header and the top bar are both 60 px tall and each ends in a 1 px `border` line: together they form **one
continuous horizontal rule** across the window (never change one height without the other).

## Nav rail (top -> bottom)
1. **Header** (60 px, 1 px bottom `border` line): logo mark 20 px (`program_info/zero-logo.svg`, compiled in as
   `:/zero/zero-logo.svg`, drawn as is: it is a full-colour tile) at x = 20, then the wordmark "ZERO" (Obsidian: 16 px, weight 600,
   letter-spacing 2 px; pixel-font themes: ZERO Pixel at 20 px, normal weight) in `text`. No caption. Collapsed: logo only, centred.
2. Top items: **Home** (`house`, Alt+1), **Library** (`library-big`, Alt+2), **Tools** (`wrench`, Alt+3). Later tasks add Discover, Worlds, Downloads, Theme Studio.
3. Stretch.
4. Bottom group: **Appearance** (`palette`, Alt+4), **More** (`ellipsis`), **Settings** (`settings`).
   - More: menu with Folders > (existing folders menu), Help > (existing help actions that are enabled), Undo trash (when
     available), About, Check for updates (only if the updater is enabled). Opens above the item.
   - Settings: opens the existing global settings dialog (not a page yet).
- Items are 36 px; the active item has an `elevated` pill + 2 px `accent2` indicator that glide between items (200 ms OutQuint;
  texture and bevel unchanged). **Tooltips** show the label and the shortcut ("Library  Alt+2"); items without a shortcut
  (More, Settings) only have a tooltip while the rail is collapsed.
- Page shortcuts are window-wide: the n-th page added to the shell gets Alt+n (Ctrl+1..N switch accounts).

## Top bar (`shell/ZeroTopBar`)
60 px, transparent (the texture shows through), 1 px bottom `border` line, horizontal padding xl, vertical centre alignment.
- **Left**: `ChromeTitle`, drawn on one text baseline: optional breadcrumb parent (Title font, `textSecondary`, `text` on
  hover, pointing-hand cursor, keyboard focusable, then a `chevron-right` 12 px in `textTertiary`), the page **title** (Title
  role: pixel font in Minecraft, `text`), then the **subtitle** 10 px after it (caption, `textSecondary`). When space is short
  the subtitle is elided first (full text in a tooltip), then the title.
- **Centre**: `setCenterWidget(QWidget*)`: the universal search pill (`shell/ZeroSearchPill`, see "Command palette"). The slot stays invisible while it is empty.
- **Right**: the current page's action widget, a 1 x 24 px vertical divider (`border`; only while there is an action widget),
  then the **AccountChip** (compact: 28 px avatar + profile name + `chevron-down` 12 px; `surfaceHover` pill on hover; focus
  ring only for keyboard focus; tooltip "<name> - <Microsoft account | Offline | No account>"). Click -> Prism's accounts
  menu, opened **below** the chip, right-aligned (`ZeroShell::accountMenuRequested(anchor)`, anchor = chip bottom-right, global).
- **Page change**: title, subtitle and actions crossfade (140 ms, `Transition::Crossfade` on the top bar; instant with motion off).
  A `chromeChanged()` that only changes the subtitle (e.g. the live instance count) swaps the text in place without a transition;
  a changed title, breadcrumb or action widget crossfades.
- Breadcrumb click -> `ShellPage::breadcrumbActivated()` of the current page.

## Home page
Top bar: title "Home", no subtitle, no actions. Content max width 1280, page padding xl, vertical gap xxl between sections.
1. **Hero** (`ZeroCard`, min height 200): instance to continue = most recently launched instance
   (`BaseInstance::lastLaunch()`), else the first instance by name.
   - Left: 96 px instance icon on an elevated 128 px rounded tile (radius lg); when the hero instance has artwork the tile is
     replaced by a 256x144 cover-cropped artwork panel (radius lg, 1 px `border`, no gradient).
   - Overline "CONTINUE PLAYING", name (display), caption meta: `<version · loader> · Played <relative time> · <total playtime>`.
   - Buttons: **Play** (primary, lg, `play` icon) → launches that instance; while running → **Stop** (danger, `square`)
     + "Running" badge (success dot). **Edit** (secondary) → instance edit window. **Folder** (ghost, `folder-open`).
2. **Favorites** (heading, only when at least one instance is a favorite): the same small cards as Recently played, max 6, sorted by
   last played (never-played ones by name after them). Above Recently played.
3. **Recently played** (heading + "View library" ghost link at right): up to 6 small instance cards, sorted by last launch,
   only instances launched at least once. Section hidden if none. Card click selects it in Library and switches page;
   card Play button launches.
4. **Recent activity** (heading; hidden when there is nothing to show): one `ZeroCard` with up to 6 rows from the session history, newest first,
   skipping instances that no longer exist. Row (48 px, clickable, `surfaceHover` on hover, 1 px `border` divider between rows): instance icon
   20 px, 6 px status dot (`success` played, `textTertiary` stopped, `danger` crashed / failed), name + outcome caption ("Played for 1 h",
   "Stopped after 12 min", "Crashed after 3 min · exit code -1" and "Launch failed" in `danger`), relative end time at right, and for crashed /
   failed sessions a ghost "Diagnose" button (`stethoscope`) that opens Crash Detective and analyzes. Row click selects the instance in Library.
5. **Shortcuts** grid (4 tiles, `ZeroCard` clickable, icon + label + caption): New instance (`plus`), Import (`package`),
   Accounts (`user`), Instances folder (`folder-open`).
- Empty library → whole page is a `ZeroEmptyState`: icon `library-big`, "Your library is empty",
  "Create an instance, install a modpack or import one you already have.", primary "New instance", secondary "Import".

## Library page
- Top bar: title "Library", subtitle "<N> instances" (live; "<shown> of <N> instances" while searching; "No instances" when empty).
  Actions (top bar): view toggle (grid `layout-grid` / list `list`, ghost checkable pair) + **New instance** (primary, `plus`).
  With an empty library the toggle is hidden; New instance stays.
- Content starts with a **toolbar row**: search field left (280 px, `search` icon, placeholder "Search instances", Ctrl+F
  focuses, Esc clears), **filter chips** in the free space (`FilterChipBar`), sort combo right (Name / Last played / Most played =
  `InstSortMode` `Name` / `LastLaunch` / `Playtime`, persisted, grid and list both follow). Hidden when the library is empty.
- **Filter chips** (single select, AND-combined with the text search; pure logic in `launcher/zero/profiles/LibraryFilter`): **All**,
  **star Favorites**, **Running** (always shown) then one chip per loader present (Vanilla, Fabric, Forge, NeoForge, Quilt, others
  alphabetically) and one per tag present (`#tag` + instance count, alphabetical). Chip: 28 px, padding 12, `panelPath` radius md,
  `surface` + `border`; active = `elevated` + `text` + a 6 px `accent2` dot (hover / active colors animate 140 ms; the dot slot is
  part of every chip so selecting never moves neighbours). The row scrolls horizontally (wheel, or `chevron` buttons that appear
  only while there is more in that direction; the active chip is scrolled into view). Clicking the active chip returns to All. Chips
  are rebuilt on instance / profile changes (0 ms debounce, widgets of unchanged chips are kept); an active chip that vanished (the
  last instance with that tag lost it, the loader is gone) falls back to All. Filtering goes through `InstanceProxyModel::setPredicate`
  so the grid, the list and Prism groups stay consistent (a group exists while any of its instances pass). The text search also
  matches tags. No match -> `ZeroEmptyState` "No instances match" with **Clear filters** (clears the search and the chip); the
  subtitle reads "<shown> of <N> instances" while a search or chip filters.
- Body: `[collection | details panel 300 px]`, gap lg.
- **Grid**: Prism's `InstanceView` (keeps groups, drag-and-drop, keyboard nav, inline rename, context menu) with a new
  `ZeroInstanceDelegate` painting cards: 168×188 cell, card radius lg, bg surface, border border; icon 64 px centered
  in the top 112 px area (bg elevated, top corners rounded); name (label 13/600, elided, 2 lines max) + caption
  (version · loader). Hover: borderStrong + surfaceHover. Selected: border accent 1px + bg elevated. Running: success dot top-right.
  **Artwork**: when the instance has artwork the top area shows it cover-cropped (top corners clipped with `panelPath`) and the instance
  icon becomes a 28 px badge (`surface`, 1 px `border`) at its bottom-left. **Favorite**: solid `star` in `highlight` top-left (on
  artwork over a translucent `bg` disc); on hover a hollow star fades in there, `highlight` under the pointer; clicking it toggles
  the favorite and is consumed (no selection change, no launch): the delegate viewport event filter emits
  `favoriteToggleRequested(instanceId)` (`InstanceView` never calls `editorEvent`).
  Group headers: overline style + count, collapsible as before.
- **List**: `QTreeView` (no root decoration, uniform rows 36 px) over the same instances: Name (icon 20 px + name),
  Version, Loader, Group, Last played (relative), Playtime. Sortable by header. Same selection as grid (one selection model source of truth via MainWindow).
- Search filters both views by name, group, version and tags (case-insensitive substring).
- **Details panel** (hidden when nothing selected → small empty state "Select an instance"): 96 px icon (click = change icon),
  name (title, double-click = rename), caption meta, stats rows (Last played, Total playtime, Group), **Play/Stop**
  (primary, full width, lg). Then **Tags** and **Description** (below), then a ghost action list with icons: Edit (`pencil`),
  Open folder (`folder-open`), Change group (`list-filter`), **Set artwork…** (`image`; file dialog png / jpg / jpeg / webp / bmp, handled
  by MainWindow), **Remove artwork** (`circle-x`, only while artwork is set), Copy (`copy`), Export ▸ (Zip / Modrinth / CurseForge),
  Create shortcut (`terminal`), Delete (danger, `trash-2`). The action buttons map to the existing MainWindow actions; disabled states mirror them.
  - With artwork the icon tile is replaced by a 16:9 cover (`ArtworkHeader`, height-for-width) with the icon as a clickable 44 px badge at its
    bottom-left (change icon). A `star` toggle sits right of the name (solid `highlight` when favorite, `textTertiary` outline otherwise;
    tooltip "Add to favorites" / "Remove from favorites"; emits `favoriteToggleRequested`). The meta caption gains "N mods" (enabled `*.jar`
    directly in the mods folder, only for instances with a mod loader; cached by folder mtime).
  - **Tags**: overline "TAGS", wrapping chips (`FlowLayout`) each with an x to remove, then an inline "Add tag" field (24 chars max, completion
    from every tag in the library, Enter adds, Esc cancels, Backspace on an empty field removes the last tag; disabled at 12 tags).
  - **Description**: overline "DESCRIPTION", `QPlainTextEdit` 3-6 lines (grows with the text, scrolls beyond 6), placeholder "Add a description or notes…";
    saved to Prism notes on focus out and after 600 ms idle, flushed before another instance is shown. An edit in progress is never overwritten
    by a refresh.
  - The panel scrolls as a whole (it does at 1100x700 and below).

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
Rail item "Appearance" (`palette`, bottom group, Alt+4); top bar title "Appearance", subtitle "Themes and motion". Max width 960, scrolls when short.
- **Theme**: `ThemePreview` (full width, 16:9, max 340 px) paints a miniature ZERO window from a `ThemeSpec` (never the active one: textured background,
  rail + active pill, header in the spec's title font, hero card with primary button bevelled/notched per spec, two cards). One card per ZERO theme
  (`ZeroTheme.h: specForThemeId`): swatches (bg, surface, accent, accent2), name, tooltip, "Current" badge on the applied one. Selecting a card
  crossfades the preview (200 ms) and applies nothing; **Apply theme** (enabled when selection != applied) emits `applyThemeRequested`, MainWindow applies
  through `ThemeManager::setApplicationTheme` and persists `ApplicationTheme` (the global theme transition plays). The page follows theme changes made elsewhere.
- **Motion**: segmented "Follow Windows" / "On" / "Off" bound to the `ZeroMotion` setting (live). Caption states what the choice means; for Follow Windows it says whether
  Windows animation effects are on or off. "Preview" replays a Rise transition on the preview (nothing with motion off).
- "More appearance options" opens Prism's global settings on `appearance-settings`.

## Tools page and Crash Detective
- `ToolsPage` (`pages/ToolsPage`): top bar title "Tools", subtitle "Utilities for keeping your instances healthy"; the content is a grid
  of tool cards (only Crash Detective for now). A card opens the tool inside the page; then the top bar shows the breadcrumb
  "Tools > Crash Detective" (clicking "Tools" goes back to the overview) and the tool's description as subtitle. There is no back button.
- `CrashDetectiveView`: has no header of its own. Its controls (spinner, instance combo 220 px, **Analyze**) are lent to the top bar as the
  page's action widget while the tool is open. Instance combo (defaults to the Library selection), **Analyze**, spinner while the work runs on a
  `QtConcurrent` thread, then one `FindingCard` per finding (severity icon, title, confidence badge, explanation, "What to try",
  action buttons, collapsible monospace evidence with source + line). Caption lists the analyzed files and their timestamps.
  No findings -> "No known cause recognised" (Open log / Open logs folder); no log files -> "No logs found".
- Badge policy: only a *Confirmed* cause is tinted (danger for Critical, warning for Warning); Likely/Possible are neutral. Never green.
- Engine: `launcher/zero/diagnostics/` (`CrashDetective`, `CrashReportCollector`); tests `tests/CrashDetective_test.cpp`.
- Crash hook: `LauncherPartLaunch::on_state` emits `Application::gameStoppedUnexpectedly(instanceId, exitCode)` when the game process
  crashes or exits non-zero (a user kill is `Aborted` and does not fire). `MainWindow` shows `ZeroToast` "<name> stopped
  unexpectedly" with action **Diagnose** -> Tools > Crash Detective for that instance, analysis started with the exit code.
- Instance settings action: `APPLICATION->showInstanceWindow(inst, "settings")` (the Edit window, Settings page).

## Command palette (Ctrl+K), search pill and shortcut sheet
- **Pill** (`shell/ZeroSearchPill`, top bar centre): 360 px (shrinks to 240), 34 px, `surface` + 1 px `border` (`panelPath` radius md), `search` 16 in
  `textTertiary`, "Search or jump to…" in `textTertiary`, a single "Ctrl K" key cap at the right. Hover: `borderStrong` + `surfaceHover` (StateAnimator).
  Click or Ctrl+K (window-wide `QShortcut`, toggles) opens the palette.
- **Overlay** (`palette/PaletteOverlay`, shared with the sheet): a child of the main window covering it. Backdrop `bg` at 55 %; panel 640 px (max 90 %),
  top edge at 14 % of the window height, `surface`, 1 px `border`, radius xl, popover shadow. Open: backdrop 160 ms, panel fade + 8 px rise + scale 0.98 -> 1
  in 180 ms OutQuint (the panel is snapshotted and painted by the overlay while it moves; the real widget waits off-screen so focus and typing work from frame 0);
  close 120 ms fade; motion off = instant. Esc or a backdrop click closes; focus returns to the widget that had it (restored when the close starts, so a
  command that opens a dialog or moves focus is not overridden).
- **Palette** (`palette/ZeroCommandPalette`): input row 52 px (`search` 18, frameless line edit in heading size, "Esc" key cap), results (max 8 rows visible,
  own scrolling, slim indicator), footer 28 px with the hints of the selected item ("↑↓ Navigate · ↵ Play · Ctrl+↵ Show in Library · Esc Close").
  Rows are 40 px: icon (instance icon 20 px / Lucide 16 px), title (matched characters weight 600 + `text`, rest `textSecondary`; no match info = all `text`), caption
  subtitle, then key caps (the real shortcut), a "Current" badge or, on the selected row, a hint ("Play ↵"). Selected row: `higher` + 2 px `accent2` bar; the highlight
  glides between rows in 140 ms. Hover selects, click runs, Up/Down/Tab/PageUp/PageDown move (wrapping), Enter runs, Ctrl+Enter runs the secondary action.
  Empty query: **Continue** (3 most recently played instances), **Navigation**, **Suggested**. With a query: sections Instances / Navigation / Actions / Settings /
  Appearance, the section holding the best match first; no match -> "No results for ...".
- **Favorites in the palette**: a favorite instance shows a solid `highlight` star after its title (and also matches the keywords "favorite" / "star"); the
  Actions section has **Toggle favorite** for the selected instance (`action:instance-favorite`).
- **Descriptors** (`palette/PaletteTypes.h`): MainWindow is the controller. `MainWindow::buildPaletteCommands()` runs **every time the palette opens** and returns
  `PaletteCommand`s (id, kind, title, subtitle, keywords, icon, shortcut, hint/badge, `run`, optional `runSecondary`). Callbacks call existing QActions/APIs
  (`APPLICATION->launch`, `QAction::trigger`, `ShowGlobalSettings`, `setApplicationTheme` ...); a command that is not possible right now is simply not listed:
  - Instances: Enter = Play (the launch call Home uses), Ctrl+Enter = show in Library. A running instance shows "Running" and Enter shows it in the Library (Prism
    ignores a second launch).
  - Navigation: Go to Home / Library / Tools / Appearance (Alt+N keycaps from `ZeroShell::pageShortcut`), Open Crash Detective (for the Library selection).
  - Actions: New instance, Import instance, Manage accounts, every enabled Folders-menu entry, Undo last instance deletion (only when enabled), Settings, About,
    Check for updates (only when the updater is enabled), Keyboard shortcuts, and for the selected instance Edit / Open folder / Rename (switches to the Library first) / Delete
    (the normal confirmation).
  - Settings: "Settings: <page>" for each global settings page (ids `launcher-settings`, `language-settings`, `appearance-settings`, `minecraft-settings`, `java-settings`,
    `accounts`, `apis`, `external-tools`, `proxy-settings`, as registered in `Application.cpp`), with keywords (java -> memory, ram, jvm ...).
  - Appearance: "Theme: <ZERO theme>" (Current badge; the normal apply + persist, the transition plays), "Animations: Follow Windows / On / Off" (`ZeroMotion`).
- **Ranking** (`launcher/zero/palette/CommandMatcher`, no widgets): case-insensitive subsequence match, whitespace in the query ignored. Per matched character +16, +24 at a word
  start (+8 at index 0), +18 when it directly follows the previous match; -(4 + gap) per gap (gap capped at 10), -0.5 per skipped leading character (cap 15), -0.2 per unmatched
  character (cap 10); exact match +300, prefix match +150. Keyword matches use the same score x 0.8 and carry no highlight. Instances get a recency boost (20, halved after 3 days),
  every kind a small weight (instance 4, navigation 3, action 2, setting/appearance 1). Order: score, kind, title (case-insensitive), input index: deterministic.
  Returns the matched title positions for highlighting.
- **Shortcut sheet** (`palette/ZeroShortcutSheet`, Ctrl+/, the "Keyboard shortcuts" command, More menu entry "Keyboard shortcuts  Ctrl+/"): same overlay style, 720 px, groups
  Navigation / Instances / App in two columns, each row a label and key caps. `MainWindow::buildShortcutGroups()` reads the real QActions' shortcuts (an action
  without a shortcut, e.g. Settings on Windows, is not listed). Key caps use English key names (`QKeySequence::PortableText`).
- **Kill instance moved to Ctrl+Shift+K** (`MainWindow.ui`): Ctrl+K belongs to the palette (two actions on one shortcut would both stop working).
- Opening a page (Alt+N, rail) closes any open overlay.

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
- Library filter: `ZERO_SCREENSHOT_CHIP=<favorites|running|loader:<Name>|tag:<name>>` selects that chip (nothing = All). Profile data for fake instances: `ZeroFavorite=true`,
  `ZeroTags="[\"pvp\", \"tech\"]"` (a value with commas must be quoted in the INI format), `ZeroArtwork=.zero/artwork.png` + the PNG, `notes=...`.
- Palette: `ZERO_SCREENSHOT_PALETTE=<query>` opens the command palette with that query (`empty` = no query), `ZERO_SCREENSHOT_SHORTCUTS=1` opens the shortcut sheet.
- Motion evidence frames (after the normal grab): `ZERO_SCREENSHOT_TRANSITION=<page id>` (`home` | `library` | `tools`)
  switches to that page and `ZERO_SCREENSHOT_THEME_TO=<theme id>` switches theme, each saving the window grab every 40 ms
  for 320 ms as `<file>-t0.png` .. `<file>-t8.png`, plus `<file>-tfinal.png` once everything settled (the last frame must
  equal the settled UI). With both set the page switch runs first, then the theme switch (files `<file>-theme-t*.png`).
  Needs motion on (default `system`; set the `ZeroMotion` setting to `on` when Windows "Animation effects" is off).
- The grab includes the top bar and the rail header (the continuous 60 px rule); `ZERO_SCREENSHOT_DIAGNOSE` shows the breadcrumb state of the top bar.
  An absolute output path works; a relative one is resolved against the data dir given with `-d`. Fake instances for screenshots need a valid
  `instance.cfg` (with a `[General]` header) and a well-formed `mmc-pack.json`, otherwise a debug ASSERT in `Task.cpp:108` stops the app.
- Tests: `tests/CommandMatcher_test.cpp` (prefix beats subsequence, word starts, gaps, aliases, recency tie-breaks, highlight positions, empty query, no match), `tests/ZeroCommandPalette_test.cpp` (open/close, sections, query, wrapping navigation, Enter / Ctrl+Enter callbacks, absent commands, Esc and backdrop restore focus, scrolling, sheet).
- Tests: `tests/InstanceProfile_test.cpp` (tag normalization, JSON round trip, favorite / notes, artwork scaling + atomic replace + clear, mod count),
  `tests/LibraryFilter_test.cpp` (every chip kind, text search, AND combination, chip building, vanished-chip fallback), `tests/ZeroLibrary_test.cpp`
  (`FilterChipBar`, `FlowLayout`, `Artwork::cover`, the card star click).
- Tests: `tests/ZeroShell_test.cpp` (top bar chrome per page, swap on page change, `chromeChanged`, breadcrumb click, Alt+N, chip anchor).
