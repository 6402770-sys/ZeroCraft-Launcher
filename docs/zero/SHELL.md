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
2. Top items: **Home** (`house`, Alt+1), **Library** (`library-big`, Alt+2), **Discover** (`sparkles`, Alt+3), **Worlds** (`earth`, Alt+4), **Screenshots** (`image`, Alt+5), **Servers** (`compass`, Alt+6), **Tools** (`wrench`, Alt+7). Later tasks add Downloads, Theme Studio.
3. Stretch.
4. Bottom group: **Appearance** (`palette`, Alt+8), **More** (`ellipsis`), **Settings** (`settings`).
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
  Actions (top bar): view toggle (grid `layout-grid` / list `list`, ghost checkable pair) + **Import** (secondary, `file-archive`, tooltip "Open a .zip or .mrpack you already have",
  emits `importRequested` = Create Instance on its Import card; icon-only with the same tooltip / accessible name "Import" when the page is narrower than 1050 px, i.e. a 1100 px window) + **New instance** (primary, `plus`).
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
  by MainWindow), **Remove artwork** (`circle-x`, only while artwork is set), Copy (`copy`), Export (opens the export chooser, no menu),
  Create shortcut (`terminal`), Delete (danger, `trash-2`). **Export chooser** (`ExportChooserDialog`, opened through `MainWindow::exportInstanceZero(id)`):
  title "Export · <instance>", three selectable cards (Full copy .zip `file-archive`, Modrinth pack .mrpack `package`, CurseForge pack .zip `blocks`; 2 px
  accent ring on the chosen one), an info box "How your friend opens it", Cancel / Continue. Enter, Continue or a double-click opens Prism's own dialog for the
  format (`ExportInstanceDialog` / `ExportPackDialog`); arrows and 1-3 move the choice; the last format is the setting `ZeroExportFormat` (zip | mrpack |
  curseforge). A snapshot instance disables the CurseForge card ("CurseForge packs can't hold snapshot versions."). Dev aid `ZERO_SCREENSHOT_EXPORT=<instance id>`
  saves the chooser alone to the `--zero-screenshot` file. The action buttons map to the existing MainWindow actions; disabled states mirror them.
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

## Worlds page
READ-ONLY hub: it lists every singleplayer world of every instance and never writes, renames, moves or deletes anything in an instance or a world
(no backup / restore / import / export / delete buttons yet; they come with a later task).
- Rail item "Worlds" (`earth`, Alt+4, right after Discover). Palette: "Go to Worlds" (keywords saves, maps). Top bar: title "Worlds", subtitle
  "N worlds in M instances" ("Scanning…" until the first scan delivered, "No worlds", "<shown> of N worlds" while the search or the instance
  filter narrows). Action: ghost **Refresh** (`refresh-cw`, tooltip "Rescan worlds"), disabled while a scan runs.
- Content starts with the Library's **toolbar row**: search field left (280 px, `search` icon, placeholder "Search worlds"; matches world name, folder name,
  instance name and version; Ctrl+F focuses, Esc clears), then **instance combo** ("All instances" + every instance that has worlds, by name; setting `ZeroWorldsInstance`,
  empty = all; an instance without worlds falls back to All without forgetting it) and **sort combo** (Last played [default] / Name / Size / Instance; setting
  `ZeroWorldsSort` = `recent` | `name` | `size` | `instance`). Hidden while there are no worlds at all.
- Body: `[list | details panel 300 px]`, gap lg. The list is one `QListView` + `WorldRowModel` + `WorldRowDelegate` (no widget per row), rows 64 px with a 2 px gap:
  40 px world icon tile (`icon.png` scaled with `Qt::FastTransformation` so pixel art stays crisp, full-bleed on an `elevated` tile with radius md; no icon: `earth`
  in `textTertiary`), name (label 13/600, elided) over `<instance> · <version>` (caption, `textSecondary`), right-aligned caption column (relative last played over the size,
  "…" until measured, both `textTertiary`). Pill badges (caption, radius sm, `surface` + 1 px `border`) sit before that column: game mode when not Survival, "Hardcore"
  (`danger` text) and "In use" with a 6 px `success` dot while the instance runs (live through `InstanceList::dataChanged`; badges that would squeeze the name below
  96 px are dropped). Rest rows are transparent, hover `surfaceHover`, selected `elevated` + 1 px `accent` border (radius md). Arrow keys / Home / End work.
- **Details panel** ("Select a world" empty state when nothing is selected): 96 px icon tile, name (title, elided), folder name caption, stats rows Instance, Game mode
  (+ " · Hardcore"), Version, Seed (monospace + a ghost `copy` button: clipboard + success toast "Seed copied"; "Unknown" when level.dat has no seed), Size ("Calculating…"
  until measured), Last played (tooltip = absolute date). Below the stats the **Backups** block (`WorldBackupsSection`, Smart Backup, see ARCHITECTURE.md), then the ghost
  actions: **Open folder** (`folder-open`, `DesktopServices::openPath`), **Open backups folder** (`folder-open`, only while `<dataRoot>/zero/backups/<instance>/<folder>` exists)
  and **Show instance in Library** (`library-big`, `MainWindow::showInLibrary`).
- **Backups block**: overline "BACKUPS" with a right-aligned caption (`textTertiary`): "No backups yet" / "1 backup" / "N backups" (empty until the first listing arrived).
  Full-width secondary button **Back up now** (`archive`); it reads "Backing up…" / "Restoring…" / "Deleting…" and is disabled while a job runs for this world, and
  disabled with tooltip "Close the game to back up this world" while the instance runs. The list (hidden when empty; 40 px rows, 5 rows visible, then it scrolls; hairline
  `border` between rows) shows the date as `Format::relativeTime` (tooltip: full local date and time) over `Format::bytes(zip size)`, a "Safety" pill (same pill as the world
  row badges) for safety snapshots or a `danger` "Damaged" pill for a zip whose manifest cannot be read, and two 28 px ghost icon buttons: **Restore** (`refresh-cw`, tooltip
  "Restore this backup", hidden for damaged ones) and **Delete** (`trash-2`, tooltip "Delete this backup"); both disabled while the instance runs or a job runs for the world.
  Restore asks "Restore world?" ("Replace “<world>” with the backup from <date>? The current world is saved as a safety backup first." Restore / Cancel, Cancel is the
  default); Delete asks "Delete this backup? It is moved to the Recycle Bin." (Delete / Cancel, Cancel default). Results are toasts: "Backup created" / "World restored" /
  "Backup deleted" (success) or the error text (error); a restore also rescans the page. The list is requested on a pool thread whenever another world is selected and after every
  job for the shown world.
- Selection is kept by the world key (`<instanceId>/<folderName>`) across rescans, filters and sorting; a world that vanished clears it. A selected world that a filter hides
  comes back when the filter shows it again.
- Empty states: no worlds at all -> `earth` "No worlds yet" / "Worlds appear here after you play singleplayer in any instance." (no button; the toolbar is hidden);
  filters without a match -> "No worlds match" + **Clear filters** (clears the search and resets the instance combo).
- Scanning (`launcher/zero/worlds/WorldScanner`): first time the page is shown, again on show when the last scan is older than 30 s, on **Refresh**, and (500 ms debounce)
  when instances are added / removed / reset or an instance was renamed. Running-state changes only update the badges. See ARCHITECTURE.md "Worlds Hub".

## Screenshots page
Gallery of every screenshot of every instance (`<gameRoot>/screenshots/*.png|jpg|jpeg`). Read-only except the viewer's "Delete…", which only moves one file to the Recycle Bin after a confirmation.
- Rail item "Screenshots" (`image`, Alt+5, right after Worlds). Palette: "Go to Screenshots" (keywords screenshots, gallery, images, pictures). Top bar: title "Screenshots", subtitle "N screenshots in M instances"
  ("Scanning…" until the first scan delivered, "No screenshots", "<shown> of N screenshots" while filtering). Action: ghost **Refresh** (`refresh-cw`), disabled while a scan runs.
- **Toolbar row** like Worlds: search (280 px, "Search screenshots"; file name + instance name; Ctrl+F, Esc clears), instance combo ("All instances" + instances that have screenshots; setting `ZeroScreenshotsInstance`),
  sort combo (Newest [default] / Oldest / Instance; setting `ZeroScreenshotsSort` = `newest` | `oldest` | `instance`). Hidden while scanning and while there are no screenshots.
- **Grid**: one `QListView` in icon mode + `ScreenshotModel` + `ScreenshotDelegate` (painted tiles, uniform grid, wrapping). Fluid: columns = as many tiles with a thumbnail >= 240 px as fit, tiles share the width evenly (first column flush left, last flush right, 16 px gap). A tile: 16:9 thumbnail (>= 240 px wide) cover-cropped with radius md, caption below: instance name (label) over the relative date (caption,
  `textSecondary`), 8 px padding, 16 px gap between tiles. Hover `surfaceHover`, selected `elevated` + 1 px `accent` border. Until the thumbnail is decoded: a `surface` placeholder with the `image` icon.
  Thumbnails come from `ThumbnailCache` (pool decoding with `QImageReader::setScaledSize`, LRU of 300, key = path + modified time, memory only).
- States: scanning ("Looking for screenshots…"), empty ("No screenshots yet" / "Press F2 in game to take one. They show up here from every instance."), no matches ("No screenshots match" + "Clear filters").
- **Viewer** (double-click or Enter on a tile): an overlay child of the page (not a window), backdrop = theme `bg` at ~99 % alpha, fades in and out with `Motion::fadeIn/fadeOut` (instant with reduced motion). The image is scaled to fit (never above 1:1; the
  thumbnail is shown until the full image is decoded on the pool). Bottom bar (panel, radius lg): file name (label) over `<instance> · <full date> · <size> · N of M` (caption), ghost buttons Copy (`copy`, disabled until the image is decoded; toast "Copied to clipboard"),
  Open (default app), Show in folder, Show instance (Library), Delete… (confirmation, then `FS::trash`; a failed trash shows an Error toast and keeps the file, there is no permanent-delete fallback; on success a toast, a rescan and the next image).
  Keys: Left / Right (previous / next in the current filtered order), Home / End, Esc or a click on the backdrop closes (focus returns to the grid tile). A page change closes it.


## Servers page
Server Vault: every multiplayer server saved in any instance's `servers.dat`, grouped by address. Read-only: it never writes `servers.dat` (adding, editing and removing servers stays in Prism's per-instance Servers page). **A ping only proves the server answered a status request; the UI never says "online" or "available".**
- Rail item "Servers" (`compass`, Alt+6, right after Screenshots; Tools is Alt+7, Appearance Alt+8). Palette: "Go to Servers" (keywords servers, multiplayer, ping, join). There is no per-server "Join <name>" palette entry: the list only exists after the page scanned, so such entries would appear and disappear depending on whether the page was opened. Top bar: title "Servers", subtitle "N servers from M instances" ("Scanning…" until the first scan delivered, "No servers", "<shown> of N servers" while filtering). Action: ghost **Refresh** (`refresh-cw`): rescans the files and pings every server again; disabled while a scan runs.
- **Toolbar** like Worlds: search (280 px, "Search servers"; names, address, instance names; Ctrl+F, Esc clears), instance combo (setting `ZeroServersInstance`), sort combo Name [default] / Players / Latency (setting `ZeroServersSort` = `name` | `players` | `latency`; the order is applied once the pings settled, not on every result; servers that responded come first). Under it one caption line: "A response only means the server answered a status request. It doesn't guarantee you can join." (plus "The servers.dat of X could not be read." when a file is broken). Hidden while scanning and while there are no servers.
- **Rows** (painted, `ServerRowDelegate`, 72 px): 48 px icon tile (the saved 64x64 PNG scaled nearest-neighbour; `compass` placeholder), name (label, demibold) / address (caption, system monospace) / "In Survival 1.20, Creative" (caption, elided). Right side: "Pinging…" caption while the ping runs; Responded = success `PillTag` + caption "123 / 500 players · 45 ms"; No response = plain pill; Invalid address = warning pill. The row tooltip repeats the disclaimer for responded servers.
- **Details panel** (300 px, like Worlds): icon, name, address + copy button (toast "Address copied"), status pill + the disclaimer, Players / Latency / Version rows, MOTD (plain text, wrapped; hidden when empty), "Saved in" (instances), primary **Play on server** and ghost **Show instance in Library**. With several instances both buttons open a menu to pick one; busy instances (running or launching) are disabled ("(running)") and the button is disabled when every one is busy or the address is invalid. Play launches exactly like Prism's Join: `Application::launch(instance, LaunchMode::Normal, make_shared<MinecraftTarget>(MinecraftTarget::parse(address, false)))`.
- Everything a server sends (MOTD, version) is shown as plain text only (`Qt::PlainText` labels), length-capped, control characters removed.
- States: scanning ("Looking for servers…"), empty ("No servers yet" / "Servers you add in Minecraft's Multiplayer menu show up here from every instance."), no matches ("No servers match" + "Clear filters").
- Pings start when the page is first shown (once per session) and on Refresh; a later rescan (instances added/removed, page shown again after 30 s) only pings addresses that have no result yet.

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
Rail item "Appearance" (`palette`, bottom group, Alt+8); top bar title "Appearance", subtitle "Themes and motion". Max width 960, scrolls when short.
- **Theme**: `ThemePreview` (full width, 16:9, max 340 px) paints a miniature ZERO window from a `ThemeSpec` (never the active one: textured background,
  rail + active pill, header in the spec's title font, hero card with primary button bevelled/notched per spec, two cards). One card per ZERO theme
  (`ZeroTheme.h: specForThemeId`): swatches (bg, surface, accent, accent2), name, tooltip, "Current" badge on the applied one. Selecting a card
  crossfades the preview (200 ms) and applies nothing; **Apply theme** (enabled when selection != applied) emits `applyThemeRequested`, MainWindow applies
  through `ThemeManager::setApplicationTheme` and persists `ApplicationTheme` (the global theme transition plays). The page follows theme changes made elsewhere.
- **Create theme / Import...** (right of the "Theme" heading; secondary and ghost buttons). The cards form a 3-column grid (built-in themes first, then custom
  ones by name; rows grow). A custom card has a "..." icon button (tooltip "Theme options", keyboard reachable, left of the "Current" badge) with **Edit**, **Duplicate**,
  **Export...**, **Delete**. Duplicate copies as "<name> copy" (`uniqueName`) and selects (does not apply) the copy; Export asks for "<name>.zip" and toasts
  "Exported <name>" with "Show in folder"; Import (`*.zip`) selects (does not apply) the new theme and toasts "Imported <name>", errors are shown verbatim; Delete asks
  "Move "<name>" to the Recycle Bin?" (restorable from there), then `trashTheme` + `ThemeManager::removeCustomTheme` (never a permanent delete; a failure keeps the folder).
  File work lives in `ThemeLibrary` (hooks into ThemeManager, so tests need no Application) over `zero/theme/ThemeFiles.h` + `ThemeArchive.h`.
- **Theme editor** (`ThemeEditorPanel`, state in `Custom::ThemeEditorModel`): a right-side panel of the page, 28 % of the page width clamped to 250-320 px, full height, `elevated`
  surface with a 1 px left border, fades in over 200 ms (instant with motion off); the page content shrinks, the panel scrolls vertically when the window is short and keeps
  Cancel / Save theme pinned at the bottom. Top to bottom: header "New theme" / "Edit theme" + "..." menu (Duplicate, Export..., Delete; only when editing a saved theme),
  name field (new default `uniqueName("My theme")`), START FROM Obsidian | Minecraft (re-derives: untouched colors take the new base's, touched ones stay, corners go back to the
  base default), COLORS (Accent with 5 preset dots from `Tokens::accentPresets`, Background, Text with a "Readable" / "Hard to read" label whose tooltip names the failed pairs; each
  row = 20 px swatch -> non-native `QColorDialog` without alpha, label, hex field that takes `#RRGGBB` / `RRGGBB` and reverts invalid text on focus-out), "All colors" (the
  19 `overrideKeys()` tokens other than accent / background / text, with human names; editing sets an override, "Reset" appears only on overridden tokens), BACKGROUND
  None | Image (the Video segment is not offered until video backgrounds exist; a stored video shows None and its kind / file stay untouched on save unless None or Image is chosen)
  with the image file name + "Change" (png jpg jpeg webp bmp gif) and the Dim (0-90 %) and Cards (40-100 %) sliders shown only for Image (None saves card opacity 1.0), CORNERS Rounded | Square.
  **Live preview is the whole app**: every change re-derives the spec and `Zero::applySpecLive` applies it (no registration, no transition), throttled to one apply per 120 ms
  (the first change starts the clock, the latest state is applied when it rings); a name change never re-applies. The picked image previews from its source path. The editor
  applies the spec at once when it opens. **Cancel / Esc** (focus inside the panel) re-applies the theme that was active when it opened through ThemeManager (even the same id) and
  writes nothing. **Save theme** (disabled while the name is empty): new -> `uniqueSlug` folder, image copied as `background.<ext>` (temp name, then rename); edit -> same folder
  (renaming keeps the id; a replaced image is written first, the old `background.*` removed after the JSON); then `loadCustomTheme`, apply + persist `ApplicationTheme` (the Apply
  button path), cards refresh and select it, panel closes, toast "Saved <name>". An error keeps the panel open with an error toast and nothing applied. While the panel is open the big
  `ThemePreview` at the top of the page shows the editor's live spec (same throttle) and returns to the selected card's theme on close; the selection ring is on the card of the
  theme being edited (on no card for a new theme) and returns to the normal selection on close (the saved theme after Save, the previous one after Cancel); the "Current" badge
  always marks the applied theme. Navigating to another page keeps the panel and the live look. No window-level Esc shortcut exists on the Appearance page, so the panel's
  Esc (a `QShortcut` scoped to the panel and its children) only fires while focus is inside it. Deleting the theme being edited closes the panel and restores the persisted theme.
- Dev aids: `ZERO_SCREENSHOT_THEME_EDITOR=new|<custom theme id>[,minecraft][,hard][,all]` (with `--zero-page appearance`) opens the editor before the grab (`,minecraft` starts from the
  Minecraft base, `,hard` picks a text color barely above the background, `,all` expands All colors); `ZERO_SCREENSHOT_THEME_MENU=<custom theme id>` paints that card's options menu onto the grab. `ZERO_SCREENSHOT_COLOR_DIALOG=1` (with the editor open) opens the editor's color dialog on the accent, picks two colors, and saves the real screen around the window as `<file>-dialog.png` (what the user sees: window opacity included) and the dialog's own grab as `<file>-dialog-widget.png`; `ZERO_SCREENSHOT_SCREEN=1` saves the top of the real screen around the window (native title bar with the window icon) as `<file>-screen.png`.
- **Motion**: segmented "Follow Windows" / "On" / "Off" bound to the `ZeroMotion` setting (live). Caption states what the choice means; for Follow Windows it says whether
  Windows animation effects are on or off. "Preview" replays a Rise transition on the preview (nothing with motion off).
- "More appearance options" opens Prism's global settings on `appearance-settings`.

## Tools page and Crash Detective
- `ToolsPage` (`pages/ToolsPage`): top bar title "Tools", subtitle "Utilities for keeping your instances healthy"; the content is a grid
  of tool cards in a 2x2 grid (Crash Detective, Storage, Java Doctor, Mod Health). A card opens the tool inside the page; then the top bar shows the breadcrumb
  "Tools > Crash Detective" (clicking "Tools" goes back to the overview) and the tool's description as subtitle. There is no back button.
- **Storage** (`pages/StorageView`, core in `zero/storage/StorageScanner`): opening it starts a scan (not when one is running). First scan: centered "Measuring disk usage..." state; later rescans keep the report and show "Updating..." in the summary card. Top bar action: ghost **Rescan** (disabled while scanning / trashing). Cards: summary (total, data folder, free space on the drive), "Where the space goes" (per category, sorted, share bars), "Instances" (per instance, bar relative to the largest, click or Enter shows it in the Library, folder button), "Clean up" (old logs / crash reports older than 30 days -> confirmation -> Recycle Bin only; World backups folder link; interrupted restore folders listed information-only, never deletable). Command palette: "Open Storage".
- **Java Doctor** (`pages/JavaDoctorView`, core in `zero/java/JavaDoctor`): opening it looks for Java runtimes through Prism's own `JavaInstallList` load task (async; first time a centered "Looking for Java runtimes..." state, later rescans keep the content). Top bar action: ghost **Rescan** (disabled while detecting). Cards: "Automatic Java" (read-only On/Off for automatic selection and downloads + ghost "Java settings", which opens the launcher settings on the Java page), "Instances" (sorted Will fail, Check, Unknown, Ready, then by name: icon, name, "Minecraft 1.20.1 · needs Java 17 or 21", a plain-language reason, a status `PillTag` and, when a compatible installed runtime exists, a "Use Java N" button; click or Enter shows the instance in the Library), "Runtimes" (one row per detected Java: version, path, "Used by N instances", 64-/32-bit and "Downloaded by ZERO" pills, "Open folder"). "Use Java N" asks "Change Java?" (Cancel is the default) and only on confirm sets what the instance Java settings page saves for a manual path (`OverrideJavaLocation` on, `JavaPath`, `AutomaticJava` off); it is refused with a toast while the instance runs or launches. Nothing is ever changed without that confirmation. Crash Detective's "wrong Java" finding has an "Open Java Doctor" action. Command palette: "Open Java Doctor" (keywords java, jdk, jre, runtime).
- **Mod Health** (`pages/ModHealthView`, core in `zero/mods`): report on the mods of one instance (read-only analysis; the only change it can make is the repair action below). Top bar action: ghost **Check again** (`refresh-cw`, disabled while scanning). An "Instance" row (label + combo of all instances, outside the cards; the choice is remembered in the `ZeroModHealthInstance` setting) sits above the content; changing it re-runs the analysis, and opening the view for an instance (palette "Check mods", Library "Check mods" button) selects it. States: "Reading mods..." while scanning; "No mod loader" for an instance without a loader (no scan); "No mods installed" + "Open mods folder" for a loader instance with no enabled jar. Summary card: icon + headline by the worst confidence (Confirmed: `circle-x` "N problem(s) will stop the game"; Likely/Possible: `triangle-alert` "N thing(s) to check"; none: `circle-check` "No problems found"), caption "Checked N mods · Fabric 0.15.11 · Minecraft 1.20.1", count pills for the non-zero levels (Confirmed danger, Likely warning, Possible info, Unknown plain) and ghost "Open mods folder". "Problems" card (Confirmed, Likely, Possible; one caption explains the levels) and, only when needed, "Couldn't check" card (Unknown): each row has the confidence pill in an aligned left column, the title, the detail, "Fix: ..." and the jar file names (elided). Repair actions (core `ModRepair`, see ARCHITECTURE "Mod Intelligence"): a finding that has actions gets a secondary button at the right of its row, "Disable mod..." (one file) or "Disable N duplicates..."; when at least two findings have actions the summary card gets a primary "Fix N problems..." (N = findings with actions) next to "Open mods folder" that applies every action at once. A click opens a `QMessageBox` "Disable mods?" ("ZERO will rename these files in the mods folder:", one line `forge-only.jar -> forge-only.jar.disabled` per file, "Nothing is deleted. You can enable them again below or from the instance's Mods page.", buttons Disable / Cancel, Cancel default). If the instance is running or launching (checked before and after the dialog) a warning toast "Close <instance> before changing its mods" is shown instead. The renames run synchronously, then a Success toast "Disabled N mod(s)" with an **Undo** action (re-enables exactly those files) and a new analysis; a failure shows an Error toast with the first error (with Undo for the renames that did work). The bottom card **Disabled mods** (hidden when there are none) lists every `*.disabled` file directly in the mods folder (name, size, ghost "Enable"); Enable refuses while the game runs and shows "<name> already exists in the mods folder" when the target exists. A mods folder with only disabled files still shows the page (summary + Disabled mods) instead of "No mods installed". Command palette: "Check mods" (keywords mods, health, conflicts, dependencies, missing) opens the view for the Library selection (else the last chosen instance). The Library details panel has a ghost "Check mods" button (`package`, below "Open folder") that opens the view for that instance; it is only shown when the instance has a mod loader (`readInstanceContext`). Below it a ghost **Roll back update** (`arrow-left`, tooltip "Back to <old version> (saved <relative time>)") appears only while the instance has a modpack snapshot that was not rolled back (Smart Backup, "Modpack snapshots" in ARCHITECTURE.md); it asks "Roll back <name> to <old version>?" (Cancel / Roll back) and is disabled while the game runs.
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
- **Pill** (`shell/ZeroSearchPill`, top bar centre): 360 px (shrinks to 200: the five Discover sections need the room), 34 px, `surface` + 1 px `border` (`panelPath` radius md), `search` 16 in
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
  - Navigation: Go to Home / Library / Discover (keywords browse, modpacks, download, content) / Worlds (keywords saves, maps) / Screenshots (keywords screenshots, gallery, images, pictures) / Servers (keywords servers, multiplayer, ping, join) / Tools / Appearance (Alt+N keycaps from `ZeroShell::pageShortcut`), Open Crash Detective (for the Library selection), Open Storage, Open Java Doctor, Check mods (for the Library selection). Actions also hold **Browse modpacks** (keywords discover, download, install, modrinth, curseforge, technic, content), which opens Discover through `MainWindow::showDiscover("modpacks")`.
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

## First-run intro (Mission 003) - as built
The first time ZERO opens on a data dir, a full-window intro plays over the main window (`ui/zero/onboarding/IntroOverlay`, a child of `MainWindow`
that follows its size, takes the focus and blocks the shell: mouse events stop at it, Tab cycles only Get started / Skip intro, and the shell's
shortcuts such as Ctrl+K stay quiet while it is up). Then the Add account dialog opens (when there is no valid account, as on any start).
- **Scene**: the installer's night scene (shared `ui/zero/ZeroScene`, see ARCHITECTURE "Shared brand scene"), the same in every theme (a brand moment, painted with the
  Minecraft palette of `ZeroTokens.h`). The mark cell is a whole number of pixels (`floor(6 * min(w / 760, h / 460))`, 3..14) and everything the installer draws scales with it; the
  content block is centred above the ground, below the Skip row, and moves up in very small windows so Get started is never clipped.
- **Timeline** (ms, one clock that starts when the overlay is created): 0-1800 sky fades in, stars twinkle, the mark builds block by block (drop-in with a small OutBack);
  1200-2400 the ground rises from the bottom; 1800-2800 the "ZERO" wordmark (pixel font) fades in and rises 12 px; 2600-3400 the caption "Welcome to ZERO Launcher"
  (Body role, secondary colour) fades in; 3200-3800 the primary **Get started** button (theme accent / onAccent, trailing `arrow-right`) fades in and gets the
  focus. The scene then idles (stars twinkle, the mark bobs 2 px at design size) until the user acts. All fades are OutCubic.
- **Skip**: top right, always, a tertiary text button "Skip intro" + an "Esc" key hint (painted with the scene colours, so it reads in every theme). Esc, Skip intro or a click anywhere
  **before the button appeared** (before 3200 ms) jumps to the final frame (button fully in); once the button has appeared Esc / Skip intro do what Get started does. Enter or Space
  on the overlay before the button exists also jump to the final frame; on the focused Get started button they press it.
- **Get started** (button, Enter, Space): `ZeroIntroSeen = true` is written at once, the overlay fades out over 300 ms (OutCubic, a snapshot of the last frame) revealing the shell, hides, deletes itself and
  emits `finished()`, which calls the single hook `MainWindow::onboardingIntroFinished()`: today it opens the Add account dialog when `Application::zeroShouldAskForAccount()` (the next onboarding
  step, the guided tour, attaches there).
- **Reduced motion** (`Theme::reduceMotion()`): no drops, slides, bobbing or twinkle; the final frame comes in with one 200 ms fade, Get started is there at once, and Get started ends the intro
  without a fade-out.
- **Gating** (`Zero::Onboarding::decideIntro`, pure, `IntroGate.h`; `Application::zeroIntroDecision()` feeds it): never in a `--zero-screenshot` run and never when the start launches an
  instance or shows an instance window directly (`--launch` / `--show`): nothing is written then. Otherwise it plays while the setting `ZeroIntroSeen` (bool, default false) is false.
  **Existing users never see it**: when the data dir already has instances or accounts and `ZeroIntroSeen` is still false (a data dir from before the intro existed), `ZeroIntroSeen` is set
  silently at startup. A data dir that this run filled by importing Prism / PolyMC / MultiMC (`Application::m_zeroDataImported`) counts as a new ZERO user and gets the intro.
  `ZERO_FORCE_INTRO=1` plays it regardless of the setting and of existing data (not in screenshot / direct-launch runs).
- **Startup order**: Prism's setup wizard (`createSetupWizard`, Java / paste / theme pages) runs before the main window exists, so before the intro. With ZERO's defaults on Windows it does not
  appear on a fresh data dir (Java auto-download is on, the theme is valid, the language fixed), so the intro is the first ZERO screen; a data dir imported from Prism with an unusual Java /
  paste-bin setup can still show the wizard first. The Prism data-import question and the temp-folder warning (modal message boxes in `Application` / `showMainWindow`) also come first; the intro
  starts when they are closed.
- Tests: `tests/ZeroIntro_test.cpp` (gating table, Esc twice, Skip button, click before / after the button, Get started once + setting + self-delete, Enter / Space, reduced motion, parent resize down to
  480 x 340, staying above the shell and later children, Tab confinement, frozen frames for screenshots; ZeroScene stars / mark / pixel sizes).

## Guided tour (Mission 003) - as built
Right after the first-run intro (and the Add account dialog, when there is one) a short tour walks through six places of the shell
(`ui/zero/onboarding/TourOverlay` + `TourSteps`). It is a child of `MainWindow` like the intro: it covers the window, follows its size, takes the keys and blocks the
shell (mouse events stop at it; Tab cycles only Skip tour / Back / Next; Ctrl+K, Alt+N and the other shortcuts stay quiet). The page does **not** change while touring, only
the spotlight moves; one step shows Library for its own sake and goes back (see step 3).
- **Look**: a dim layer (the theme's `bg` at 55 %) over everything with a **spotlight** cut out around the step's control (8 px padding, corner style and radius from the theme through
  `Paint::panelPath`), plus a 2 px `accent` ring just outside the cut-out. A **callout card** (300 px, `elevated` surface, 1 px `border`, `radiusXl`, 16 px padding, shadow, an 8 px
  pointer toward the control) sits beside it: overline "STEP n OF N" (tertiary), title (Heading role), text (Body, secondary, wraps, never more than 3 lines),
  footer "Skip tour" (ghost) left, "Back" (secondary, hidden on step 1) and "Next" (primary, "Finish" on the last step) right. Right of rail items (vertically centred on the item), below
  top-bar controls (horizontally centred), always clamped inside the window (16 px margin; `TourSteps::placeCard`, pure and tested). Colours, radii, spacing and fonts all come from the theme.
- **Steps** (`tourStepInfos()`): 1 rail Home; 2 rail Library; 3 **New instance** (the Library top-bar button: Home has no such control in its top bar, so this step shows Library
  and goes back to the page the tour started on when it ends or moves on); 4 rail Discover; 5 the search pill ("Press Ctrl+K ..."); 6 rail Appearance. A step whose target is
  missing or hidden is dropped when the tour starts (the "n of N" count adjusts) and skipped if it disappears while the tour runs.
- **Motion**: moving to another step glides the spotlight (220 ms OutCubic) while the old card fades out and the new one fades in and rises 8 px (220 ms OutCubic). The target's
  geometry is re-read on window resize, on its move / resize / show / hide and every 100 ms as a safety net (a page change takes a moment to lay out). Reduced motion: everything is instant.
- **Keys**: Right or Enter = Next (Enter on a focused Skip tour / Back button presses that button), Left = Back, Esc = Skip tour; focus starts on Next; key repeat is ignored.
- **End**: Finish and Skip write `ZeroTourDone = true` (once), fade the overlay out (200 ms), delete it and emit `finished(completed)`. Finish also shows the toast "You're all set";
  Skip shows nothing. The page the tour started on is restored.
- **When it starts**: `MainWindow::onboardingIntroFinished()` runs the Add account dialog when needed (the same condition as a normal start) and starts the tour when that dialog returns
  (account added or not), or straight away when no dialog is needed, only while `ZeroTourDone` is false. It never starts by itself on a later run, and never in a `--zero-screenshot` run
  (those have no intro). A data dir that was silently marked intro-seen at startup (an older ZERO data dir) is marked `ZeroTourDone` silently too, so existing users never see it by themselves.
- **Replay**: the command palette entry "Take the tour" (`action:take-tour`, icon `compass`, keywords tour / help / guide / tutorial / welcome / intro) starts it from step 1 on Home
  whatever the setting says (`MainWindow::startGuidedTour`).
- Tests: `tests/ZeroTour_test.cpp` (step list and skipping, keys, Finish / Skip once + setting + self-delete, fade-out, enter / leave callbacks, the card inside the window at 1100x700 / 1280x800 /
  1920x1080 for every step and never over the spotlight, the spotlight following a moved target after a resize and gliding with motion, pure card placement, the palette entry).

## Dev aids
- First-run intro: `ZERO_FORCE_INTRO=1` (normal run) plays the intro on any data dir, whatever `ZeroIntroSeen` says. `ZERO_SCREENSHOT_INTRO=<ms>` (with `--zero-screenshot <file>`, `ZERO_SCREENSHOT_SIZE`,
  `ZERO_SCREENSHOT_THEME`) saves the main window with the intro frozen at that time (no timer, no idle motion; 600 = mid-build, 1500 = ground rising, 2600 = wordmark, 4500 = final frame).
  The main window is never smaller than 1100 x 700, so `ZERO_SCREENSHOT_SIZE=900x600` yields 1100 x 700; the small-window layout is covered by `tests/ZeroIntro_test.cpp`.
- Guided tour: `ZERO_FORCE_TOUR=1` (normal run) starts the tour once the main window shows, whatever `ZeroTourDone` says (not in screenshot runs; with `ZERO_FORCE_INTRO=1` too it starts after the intro).
  `ZERO_SCREENSHOT_TOUR=<1-6>` (with `--zero-screenshot <file>`, `ZERO_SCREENSHOT_SIZE`, `ZERO_SCREENSHOT_THEME`) saves the main window with the tour settled on that step (no glide; nothing is written).
- `--zero-screenshot <file.png>` (+ optional `--zero-page <home|library|discover|worlds|screenshots|servers|tools|appearance>`): show main window, wait for layout, `grab()` to PNG,
  quit. Skips the setup wizard. Always combine with `-d <temp dir>` (never the real data dir).
- Screenshot-mode environment variables (dev only, read by `MainWindow::zeroScreenshot`): `ZERO_SCREENSHOT_SIZE=<w>x<h>`
  (default 1280x800), `ZERO_SCREENSHOT_THEME=<theme id>` (`zero-obsidian` | `zero-minecraft`: applies that theme through
  `ThemeManager::setApplicationTheme` before grabbing), `ZERO_SCREENSHOT_RUNNING=<instance id>` (flips that instance to running through the normal signal
  path), `ZERO_SCREENSHOT_SEARCH=<text>` (types into the Library search). List mode is the `ZeroLibraryView=list` setting.
  `ZERO_SCREENSHOT_TOOL=storage` (with `--zero-page tools`) opens Tools > Storage and grabs once the scan finished (5 s max; `ZERO_SCREENSHOT_SCROLL=1` scrolls the report to its end first).
  `ZERO_SCREENSHOT_TOOL=java` (with `--zero-page tools`) opens Tools > Java Doctor and grabs once the first evaluation is on screen (8 s max). Fake instances get their Java requirement from `meta/net.minecraft/<version>.json` in the data dir (`{"compatibleJavaMajors":[17,21]}`); a `JavaPath` / `OverrideJavaLocation=true` in `instance.cfg` gives a problem row.
  `ZERO_SCREENSHOT_TOOL=mods` (with `--zero-page tools`; `ZERO_SCREENSHOT_MODS_INSTANCE=<instance id>` picks the instance) opens Tools > Mod Health and grabs once the report (or an empty state) is on screen (8 s max). Fake instances need `patches/net.minecraft.json` (and the loader's patch) next to `mmc-pack.json` on the Library page, otherwise Prism tries to download metadata and a debug ASSERT in `Task.cpp:108` stops the app.
  `ZERO_SCREENSHOT_DIAGNOSE=<instance id>` opens Tools > Crash Detective for that instance and analyzes it
  (`ZERO_SCREENSHOT_DIAGNOSE_EXIT=<code>` passes an exit code, `ZERO_SCREENSHOT_EVIDENCE=1` expands every evidence block).
- `ZERO_SCREENSHOT_ADD_ACCOUNT=<choose|microsoft|ely|offline|error|success|ely-password|ely-2fa|ely-wrong>` saves the **Add account** dialog alone (docs/zero/ARCHITECTURE.md, "Add account dialog") in that state to the `--zero-screenshot` file: scripted flows with the fake code `ZKTP-GLWR` and fake links (no network, no browser), Microsoft and Ely.by both offered; `offline` types a sample name; `success` stays on "Signed in as Steve" (the 900 ms close is stretched). `ely` is the code step (needs a client ID, the build default has one); `ely-password` / `ely-2fa` / `ely-wrong` are the email + password form, its two-factor field and the wrong-password row. Combine with `ZERO_SCREENSHOT_THEME` for the other theme.
- `ZERO_SCREENSHOT_LAUNCH_PHASE=<instance id>:<preparing|running>` emits `Application::launchPhaseChanged` for that instance 300 ms after start (the signal the Play
  buttons listen to), `ZERO_SCREENSHOT_SELECT_THEME=<theme id>` selects a card on the Appearance page (nothing applied), `ZERO_SCREENSHOT_TOAST=1` shows one toast of each kind.
- Worlds: `ZERO_SCREENSHOT_WORLD=<instanceId>/<folder>` selects that world (the key is `<instance id>/<world folder name>`), `ZERO_SCREENSHOT_WORLD_SEARCH=<text>` types into the Worlds search. With `--zero-page worlds` the grab waits until the scan (sizes included) finished, 3 s at most. Fake worlds: `<instance>/minecraft/saves/<folder>/level.dat` (gzipped NBT; `icon.png` optional).
  `ZERO_SCREENSHOT_PACK_SNAPSHOT=<instance id>` (+ `ZERO_SCREENSHOT_PACK_NEW_VERSION=<text>`) takes a modpack snapshot of that instance so the Library details show **Roll back update**, and
  `ZERO_SCREENSHOT_PACK_ROLLBACK=<instance id>` grabs the roll back question instead of the page (see ARCHITECTURE "Modpack snapshots"; the fake instance needs `ManagedPack*` keys in `instance.cfg`);
  `ZERO_SCREENSHOT_BACKUP=<instanceId>/<folder>` creates (synchronously, before the page is grabbed) two manual backups and one safety backup of that world in the normal backups
  folder of the data dir, so the Backups block has rows to show (run it once; every run adds three).
- Screenshots: `--zero-page screenshots` waits for the scan and the thumbnails of the visible tiles (4 s at most). `ZERO_SCREENSHOT_VIEWER=<file name>` opens the viewer on the first screenshot with that file name and waits for its full image before the grab. Fake screenshots: PNG / JPG files in `<instance>/minecraft/screenshots/` (the file's modified time is the date shown).
- Servers: `--zero-page servers` waits for the scan and for every ping to settle (6 s at most; a refused local port can take ~4 s on Windows). `ZERO_SCREENSHOT_SERVER=<address>` selects that server (any spelling, it is normalized). Fake data: `<instance>/minecraft/servers.dat` (uncompressed NBT, root compound with a `servers` list of compounds `name` / `ip` / `icon` = base64 PNG); to see "Responded" run a tiny local fake status server on 127.0.0.1 and save `127.0.0.1:<port>` in the fixture (never ping real servers from fixtures).
- Library filter: `ZERO_SCREENSHOT_CHIP=<favorites|running|loader:<Name>|tag:<name>>` selects that chip (nothing = All). Profile data for fake instances: `ZeroFavorite=true`,
  `ZeroTags="[\"pvp\", \"tech\"]"` (a value with commas must be quoted in the INI format), `ZeroArtwork=.zero/artwork.png` + the PNG, `notes=...`.
- Discover (`--zero-page discover`; the grab waits for the first page, the featured strip, the visible pictures and any requested details, 12 s at most each): `ZERO_SCREENSHOT_DISCOVER_SECTION=<modpacks|mods|resourcepacks|shaders|worlds>` shows that section first (resourcepacks / shaders / worlds: see "Discover › Resource Packs - as built", "Discover › Shaders - as built" and "Discover › Worlds - as built"; `_INSTALL=1` there opens the add dialog of the first result, `=accept` really installs into the `-d` data dir) (Mods: the target is the first mod-ready instance of the `-d` data dir unless a Library selection or `ZeroDiscoverTarget` says otherwise; without a mod-ready instance the grab is the empty state after 0.6 s; a fake mod-ready instance needs `mmc-pack.json` with net.minecraft + a loader component and `patches/<uid>.json` for each, and an installed mod is `<instance>/.minecraft/mods/.index/<slug>.pw.toml` plus its jar; `ZeroDiscoverModsView=list` in the data dir's `zerolauncher.cfg` gives the list view), `ZERO_SCREENSHOT_DISCOVER_QUERY=<text>` types into the search, `ZERO_SCREENSHOT_DISCOVER_PROVIDER=<modrinth|curseforge|technic>` preselects the Provider facet, `ZERO_SCREENSHOT_DISCOVER_SELECT=<row>` opens the details of that row (0-based, after the first page and its pictures; the grab waits for the details and versions too), `ZERO_SCREENSHOT_DISCOVER_FILTERS=1` opens the filter panel (the popup is composited into the grab: `DiscoverPage::paintOverlays`), `ZERO_SCREENSHOT_DISCOVER_TAB=<overview|gallery|versions|whatsnew>` picks the details tab (with `_EXPAND=<n>` the n-th version row is opened in Versions, with `_VIEWER=<n>` the picture viewer opens on gallery image n), `ZERO_SCREENSHOT_DISCOVER_INSTALL=1` (with `_SELECT=<row>`) grabs the install dialog of that pack instead of the page, then rejects it; `ZERO_SCREENSHOT_DISCOVER_INSTALL=accept` presses Install after the grab and really installs the pack into the `-d` data dir; with `ZERO_SCREENSHOT_DISCOVER_SECTION=mods` the same two values act on the selected MOD: its primary action (Add to / Update) is pressed, the mod install dialog is grabbed once the versions and the dependency check settled (20 s at most) and rejected, or with `accept` the mod and its dependencies are really downloaded into the target's mods folder in the `-d` data dir (jars + `.index/*.pw.toml`) and the Discover page is grabbed 3 s later as `<file>-after.png` (its primary action then reads "Installed"), `ZERO_SCREENSHOT_TRANSITION=discover-cards` replays the Modpacks card entrance and saves the frames `<file>-t0..t8` / `-tfinal` (`ZERO_SCREENSHOT_TRANSITION_STEP=<ms>` spaces the frames, default 40). The filter and the view are persisted settings (`ZeroDiscoverModpacksFilter`, `ZeroDiscoverModpacksView`), so a run with a provider or a query leaves it in the data dir's `zerolauncher.cfg`: use a throw-away data dir per scenario or delete those lines. The network is used (Modrinth, CurseForge if the build has its key, Technic); never against the real data dir.
- Palette: `ZERO_SCREENSHOT_PALETTE=<query>` opens the command palette with that query (`empty` = no query), `ZERO_SCREENSHOT_SHORTCUTS=1` opens the shortcut sheet.
- Motion evidence frames (after the normal grab): `ZERO_SCREENSHOT_TRANSITION=<page id>` (`home` | `library` | `worlds` | `tools`)
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
- Tests: `tests/WorldBackup_test.cpp` (Smart Backup: round trip, verification, zip-slip, restore and its failure paths, retention, listing, free-space refusal, job runner and launch lock).
- Tests: `tests/WorldScanner_test.cpp` (scan of real folder layouts, skipped entries, read-only guarantee, folder size without following links, search / sort, scanner generations, destruction while jobs run).
- Tests: `tests/ZeroDiscover_test.cpp` (paging offsets, no duplicate page, end of results, stale search / selection answers dropped by generation, provider metadata in cards, Installed flag, featured strip label rules, details tabs without data, versions timeout fallback, persisted filter without the query, grid columns, reveal only for new rows; fake sources, offscreen, no network).
- Tests: `tests/ZeroShell_test.cpp` (top bar chrome per page, swap on page change, `chromeChanged`, breadcrumb click, Alt+N, chip anchor).

## Session 6 additions
- **Rail**: Settings is a page (last item, after More; Ctrl+,). Every item's tooltip says what the section is for. The
  icon of the section you open pops. The rail logo is the pixel mark under the Minecraft theme.
- **Settings page** (`pages/ZeroSettingsPage`): top bar "Settings" + current section as subtitle; ZERO page frame (side
  list with summaries, explanation under the title, help strip "Point at an option, or move to it with Tab, to see what
  it does."), footer "Changes are saved when you leave Settings.". Dev aid: `--zero-page settings`,
  `ZERO_SCREENSHOT_SETTINGS=<page id>`.
- **Instance window**: title = instance name; header card (icon, name, "1.20.1 · Forge", Running / Not running pill);
  same frame and help strip; buttons Help (ghost), Play (primary, with the arrow menu), Restart, Stop (danger), Close.
- **Worlds / Servers**: star column at the right of each row (solid gold = favorite, outline on the hovered row; a click
  toggles it without selecting the row); "Add to favorites" / "Favorite" in the details panel; favorites first in every
  sort; ghost toggle "Favorites" before the instance filter shows only favorites (remembered); "Clear filters" turns it off.
- **Loading states**: Screenshots (tiles), Servers and Java Doctor (rows), Storage and Mod Health (report) show a caption
  over skeleton shapes with a sweeping highlight instead of a centred "Looking for..." state.
- **Motion**: Appearance → Motion order On (default) / Follow Windows / Off. Page changes cascade in; lists and grids
  fill in a diagonal wave on first fill, filter and sort changes (not while typing a search); screenshot thumbnails fade
  in from the placeholder; Storage bars fill.

## Discover page (Mods section and the switcher) - as built
Binding spec: `docs/zero/CONTENT-MODS.md` ("Target instance", "Section switcher", "Discover > Mods"). `DiscoverPage` now stacks the Modpacks and Mods sections; the top bar action is a `SegmentedControl` (reused from the filter panel) **Modpacks · Mods**, read from `ZeroDiscoverSection` on start and written on every switch / `showSection`. `MainWindow::showDiscover("mods", provider)` and the palette entry **Browse mods** open it; Resource Packs, Shaders and Worlds: see "Discover › Resource Packs - as built", "Discover › Shaders - as built" and "Discover › Worlds - as built" (a segment appears only when the page was built with that environment; Worlds only with a CurseForge key).
- **Target** (`ModsSection`, `ModsEnvironment`): the instance mods are added to, chosen by `DiscoverPage::refreshTargets`: `chooseTarget(targets, Library selection, ZeroDiscoverTarget)`; the Library selection is a function `MainWindow` sets (`setLibrarySelection`) and is followed again whenever it changed since the page was last shown; a pick in the control (`TargetInstanceButton`, left of the search) is saved to `ZeroDiscoverTarget`; instance list changes (rows, data, reset; 150 ms debounce) re-read the targets: the current one stays while it exists and can take mods. `targetFromInstance` loads an instance's pack profile offline when nobody did yet (`PackProfile` reads `mmc-pack.json` lazily; Prism's status bar does the same). While the details panel is open the control may shrink to icon + chevron (tooltip "For <name> · <version> <loader>").
- **Defaults**: the target's Minecraft version and loaders are the default chips; changing the target replaces them (other facets stay), refreshes the facets (`refreshDefaults` now rebuilds them), sets the sources' `VersionFilter`, searches again, re-reads the installed mods and asks the open item's versions again (answers asked under the old filter are dropped: `ContentSection::m_versionFilterGeneration`). The version / loader selections are never persisted (`ZeroDiscoverModsFilter` keeps the other facets and the sort). "Show everything" removes them; the empty-result state then also offers it. "Update available" is hidden (no update data yet).
- **CurseForge** only with `SupportsFlame` (the page gives the section a CurseForge source only then) and `FlameAPI::validateModLoaders(target loaders)`; otherwise the Provider facet is shown disabled with the reason and a CurseForge selection falls back to Modrinth.
- **Installed** = the target's `loaderModList()` packwiz metadata (`zero/content/InstalledMods`): key `modrinth:<project id>` / `flame:<project id>` (same shape as `managedKey`). The model is loaded with `startWatching()` while the section is shown; `startWatching()` is a toggle, not a counter, so the section stops it only when its own call started it.
- **Primary action**: "Add to <name>" (name elided at 160 px), "Installed" (disabled, success look through `zeroInstalled`) when the installed file id is the newest compatible version (or the newest release), "Update" when it is older, disabled with "Close <name> to add mods" while the target runs. It only emits `ModsSection::installRequested(item, targetInstanceId, installedFileId)` (forwarded as `DiscoverPage::modInstallRequested`; nothing connects it yet: the install review is the next task). The Versions tab lists only versions that fit the target (server-side filter plus `ModsSection::versionFits` as a safety net: `ContentSection::panelVersions` hook). Gap: no "Show all versions" toggle (the panel has none and the sources would need an unfiltered request).
- **Cards** (`ModRows`, `ModDelegate`): 300-420 px wide (a single column may grow to 560 so no strip stays empty beside the open panel), 132 px high, no artwork; list rows 56 px. The environment pill comes from `IndexedPack::side` (Modrinth's `client_side` / `server_side`; CurseForge sends none: no pill). `ZeroSkeleton::setCompactCards(true)` is the skeleton in the mod card shape. The details panel has no hero picture unless the project has a featured gallery image (`DetailsHeader::compactHero`, `placeholderIcon = "blocks"`).
- **No mod-ready instance**: the section body is `ZeroEmptyState` (`blocks`) with **Create instance** -> `createInstanceRequested()` -> `MainWindow` opens Create Instance on Modded through the existing `m_zeroCreateHook`.
- **Settings** (`Application.cpp`): `ZeroDiscoverModsFilter`, `ZeroDiscoverModsView` ("grid" / "list"); `ZeroDiscoverSection` is read now.
- Tests: `tests/ZeroDiscoverMods_test.cpp`.

## Discover › Resource Packs - as built
Binding spec: `docs/zero/CONTENT-PACKS.md` ("D1 Discover > Resource Packs"; the "Turn it on" checkbox and everything that writes `options.txt` arrive with D2, there is no stub of it). The switcher is **Modpacks · Mods · Resource Packs · Shaders** (`ZeroDiscoverSection` value `resourcepacks`, segment only when the page was built with a packs environment; Shaders: see "Discover › Shaders - as built"); palette **Browse resource packs**; `MainWindow::showDiscover("resourcepacks", provider)` / `showDiscoverPacks(instanceId, query)` (-> `DiscoverPage::showPacksFor`).
- **One class, two kinds**: `PacksSection` is `ModsSection` with `ModsEnvironment::kind = Kind::ResourcePack` (`ModsSection::isFolderKind()` / `needsLoader()`; Shaders use the same two); everything kind-independent stays in `ContentSection`. `ContentSectionSpec` got `skeletonShape` (Gallery) and `viewModes` (false = grid only: no grid / list switch, a saved "list" is ignored).
- **Target**: `TargetInstance::packReady()` (any Minecraft version, no loader) / `readyFor(needsLoader)`; `chooseTarget(..., needsLoader)`; `TargetInstanceButton::setNeedsLoader(false)` lists every instance with a version and shows "· 1.21.1" only (`summaryFor`). `ZeroDiscoverTarget` is shared with Mods; each section validates the same id against its own rule (a vanilla instance is a pack target and not a mod target). No target: `ZeroEmptyState` "No instance yet" / "Resource packs need an instance." + **Create instance** -> `createVanillaInstanceRequested()` -> Create Instance on Vanilla.
- **Search** (`toSearchArgs`, unchanged): `type=ResourcePack`, `versions={mc}`, `loaders` never set, categories / features / resolutions as Modrinth category groups; CurseForge sends `versions` (one) and `categoryIds` and its Resolution facet is disabled with "CurseForge has no resolution data". Version lists: `VersionFilter{mcVersions={mc}, loaders={}}` (`panelVersions` / `versionFits` check the Minecraft version only; Modrinth pack versions have the loader "minecraft").
- **Defaults**: the target's Minecraft version is the one default chip ("Show everything" removes it, `ZeroDiscoverResourcePacksFilter` keeps the other facets and never the version); a change of the target's loaders alone does not search again.
- **Installed**: `InstanceModsSource(parent, InstalledFolder::ResourcePacks)` follows `resourcePackList()` (watched only while the section is shown) and reads `resourcepacks/.index` with the pure `installedFromIndex(indexDir, filesDir)` (entries whose file, or `.disabled` copy, is gone are skipped). Mods keep reading `loaderModList()` metadata.
- **Cards** (`PackRows`, `PackDelegate`): 260-340 px wide (single column up to 440), 16:9 artwork (the item's `artworkUrl`: featured gallery image, else the first one; none: the icon at 64 px on the elevated tile, nearest-neighbour while the source is <= 64 px) + 92 px body: icon 32, name, author, footer = resolution pills (`PackDelegate::resolutionPills`: at most 2 and "+N"), downloads, **Installed** pill at the right; provider badge on the artwork corner. Skeleton `Shape::Gallery`, reveal `RevealSpec::gallery()` (20 px / 300 ms / 28 ms).
- **Details panel / primary action**: shared; header pills = Minecraft range + the resolutions (no loaders / environment), placeholder glyph `package`, hero = featured, else first gallery image. Add to <name> / Installed / Update; "Close <name> to add packs" while the target runs (the add is refused then: the file could be added, but that is D2's decision together with "Turn it on").
- **Add dialog**: `ModInstallDialog` with `ModInstallEnvironment::packs = true`: no Dependencies block and no resolver, versions filtered by Minecraft version alone, "Adding to <icon> <name> · 1.21.1", primary "Add pack" / "Update" (`packPrimaryActionText`), plan = `buildPackInstallPlan` (the pack alone). `MainWindow::installResourcePack`: one `ResourceDownloadTask(pack, version, inst->resourcePackList(), indexed, reason)` in a `ConcurrentTask` under a `ProgressDialog` (as `installMod`), `resourcePackList()->update()`, toast "Added <name> to <instance>" with **Show packs** (instance window page `resourcepacks`).
- **Dev aid**: `ZERO_SCREENSHOT_DISCOVER_SECTION=resourcepacks` (with `--zero-page discover`); `ZERO_SCREENSHOT_DISCOVER_INSTALL=1` selects the first result on its own (no `_SELECT` needed), presses the primary action and grabs the add dialog; `=accept` really installs into the `-d` data dir.
- Tests: `tests/ZeroDiscoverPacks_test.cpp`.

## Discover › Shaders - as built
The fourth Discover section: the Resource Packs one over the `shaderpacks` folder, with the filter core's shader facets and one addition, the shader mod notice. Switcher **Modpacks · Mods · Resource Packs · Shaders** (`ZeroDiscoverSection` value `shaders`, segment only when the page was built with a shaders environment); palette **Browse shaders**; `MainWindow::showDiscover("shaders", provider)` / `showDiscoverShaders(instanceId, query)` (-> `DiscoverPage::showShadersFor`). Setting `ZeroDiscoverShadersFilter` (facets kept, never the Minecraft version and never the Platform: both follow the target).
- **One class, folder kinds**: `ShadersSection` is `ModsSection` with `ModsEnvironment::kind = Kind::Shader`. `isPacks()` is gone: `isFolderKind()` (Resource Packs and Shaders: any instance with a Minecraft version is a target, no loader in searches, `PackDelegate` gallery cards, grid only, the pack mode of the add dialog), `isShaders()`, `needsLoader()`; the nouns of notes and texts are chosen per kind. Resource Packs behave as before (`ZeroDiscoverPacks` and `ZeroDiscoverMods` pass unchanged). `MainWindow::installResourcePack` / `installShaderPack` are two names for one `installFolderContent(Kind, ...)` (`resourcePackList()` vs `shaderPackList()`; "Turn it on" only for resource packs).
- **Search** (`toSearchArgs`, unchanged): `type=ShaderPack`, `versions={mc}`, `loaders` never set. Modrinth: the Platform values (iris / optifine / canvas / vanilla), the Performance values and the Features / Category values are category groups (Modrinth indexes a shader's loaders as categories, so the platform is a `categories:iris` group, not a `loaders` argument: Prism's `ModrinthAPI` has no field for non-mod loaders). CurseForge (class 6552): one `versions` entry and `categoryIds`; Platform and Performance are disabled with "CurseForge has no shader platform data" / "CurseForge has no performance data" (the filter core's wording) and never sent. The filter core has no Provider facet for shaders; `ModsSection::sectionFacets` prepends one only when the build has CurseForge.
- **Target and defaults**: as packs (`TargetInstance::packReady()`). Default chips: the target's Minecraft version and, when its mods folder holds a shader mod, the Platform of it (Iris or Oculus -> Iris, OptiFine -> OptiFine). "Show everything" removes both. The platform is a default like the loader of Mods: it follows the target and is never persisted.
- **Shader mod notice** (`zero/content/ShaderMod.{h,cpp}`, pure; `ui/zero/content/ShaderModNotice`): `TargetInstance::modsDir` = `<game root>/mods`; `modFileNamesIn` lists its files, `shaderModFacts` decides `hasShaderMod` by a case-insensitive "iris" / "oculus" / "optifine" in a file name, `.disabled` files do not count. Without one, an `info` strip (tinted bar like the running banner, `info` icon) sits above the results (the section's accessory slot): Fabric / Quilt / NeoForge "<Instance> has no shader mod. Shaders need Iris." + **Find Iris**; Forge "... Shaders need Oculus." + **Find Oculus**; no loader "Shaders need Iris or OptiFine, which need a modded instance." (no button); another loader (Legacy Fabric ...) says nothing. The button emits `findModRequested(targetId, "Iris" | "Oculus")` -> `DiscoverPage::findShaderModRequested` -> `MainWindow::showDiscoverMods(targetId, query)`. Re-read when the target changes, when the instance list changes and when the section is shown; a changed platform re-applies the default chip and searches again.
- **Cards** (`PackDelegate(images, Kind::Shader)`): the packs' 16:9 gallery card (featured image, else the first gallery image, else the icon); footer pills = performance label (Modrinth only: `performanceLabel` of the tags, "Lightweight" / "Balanced" / "High quality" or "Lightweight – High quality", never a number) + platforms from the loader names (`shaderPlatformLabels`: Iris, OptiFine, Canvas, never "vanilla"), downloads, **Installed**. Details panel as packs; header pills = Minecraft range + the same performance / platform pills; placeholder glyph `package`.
- **Add dialog**: `ModInstallEnvironment::shaders = true` (also `packs`): header "Shader", primary **Add shader** / **Update** (`shaderPrimaryActionText`), no "Turn it on", a caption under the version row "Then pick it in the game's shader settings." (nothing is written to the game's shader config: selecting the active shader is not verified), plan = `buildPackInstallPlan`. Refused while the target runs ("Close <name> to add shaders"), re-checked after the dialog. Toast "Added <name> to <instance>" with **Show shaders** -> instance window page `shaderpacks`.
- **Installed**: `InstanceModsSource(parent, InstalledFolder::ShaderPacks)` follows `shaderPackList()`; its `indexDir()` is the shaderpacks folder itself, so `installedFromIndex(dir, dir)` reads the `*.pw.toml` files next to the shader files (they are metadata, never entries; an entry whose file or `.disabled` copy is gone is skipped).
- **Dev aid**: `ZERO_SCREENSHOT_DISCOVER_SECTION=shaders` with `_INSTALL=1|accept`, `_SELECT`, `_FILTERS`, `_QUERY`, `_PROVIDER` as for resource packs.
- Tests: `tests/ZeroDiscoverShaders_test.cpp`.

## Discover › Worlds - as built
The fifth Discover section: downloadable maps from **CurseForge only** (Modrinth hosts no worlds), the Shaders section's code over CurseForge's world class (id 17, Prism's `ResourceType::World`). Switcher **Modpacks · Mods · Resource Packs · Shaders · Worlds** (`ZeroDiscoverSection` value `worlds`). The segment exists only when the build has a CurseForge key (`DiscoverPage` builds the Worlds environment only with `Application::SupportsFlame`; without it there is no segment, no section, no stub, no palette entry, and a remembered `worlds` starts on Modpacks); palette **Browse worlds**; `MainWindow::showDiscover("worlds")` / `showDiscoverWorlds(instanceId, query)` (-> `DiscoverPage::showWorldsFor`). Setting `ZeroDiscoverWorldsFilter` (facets kept, including the Minecraft version the user picked: there is no default one).
- **One class again**: `WorldsSection` is `ModsSection` with `ModsEnvironment::kind = Kind::World` (`isWorlds()`, also `isFolderKind()`: any instance with a Minecraft version is a target, no loader, `PackDelegate` gallery cards, grid only, `earth` as the placeholder glyph). The section drops the cart of its environment (`WorldsSection::withWorlds`): no check boxes, no "Add to cart", `InstallCart::takesKind(Kind::World)` is false, `DiscoverPage::cartTargetSection()` skips it. Nothing is "Installed": worlds carry no metadata, adding a map twice makes two copies, by design (no installed source, no Installed facet or pill).
- **Filter core** (`ContentFilter`, `Kind::World`): facets **Minecraft version** and **Category** (CurseForge's own categories of class 17, read from `/categories?gameId=432&classId=17`, never hard-coded); no Provider facet (`ContentSection::sanitized` pins the provider to CurseForge for this kind, whatever a saved filter says; the section default provider is CurseForge), no loader, no features. Sorts: CurseForge's (Featured, Popularity, Downloads, Recently updated, Name). `toSearchArgs`: `type=World`, `versions` (one, CurseForge takes one) only when the user picked a version, `categoryIds`.
- **No default version chip**: an older world opens in a newer game and the filter would hide most maps. The target's Minecraft version is not a default; `VersionFilter` is empty (the Versions tab and the add dialog list every file), `versionFits` / `notForText` accept everything, a target change keeps the user's chips, "Show everything" clears them (and keeps the provider).
- **Cards** (`PackDelegate(images, Kind::World)`): the packs' 16:9 gallery card (featured image, else the first gallery image, else the icon); footer pills `worldPills`: up to two category names (`IndexedPack::categories`, CurseForge's display names), then **Made for 1.20.1** = the newest release among the project's Minecraft versions (`newestReleaseVersion`, real metadata only: snapshots and loader tags are ignored, omitted when unknown), then the downloads. The details header carries the same pills. The primary action in the details panel is **Add to <name>** as soon as there is a target that does not run (the dialog reads the files itself); "Close <name> to add worlds" while it runs.
- **Add dialog** (`ModInstallDialog`, `ModInstallEnvironment::worlds = true`, also `packs`): header "World", version combo of **every** file of the project (newest first as CurseForge sends them, no filtering), primary **Add world**, no "Turn it on", no dependencies, no shader note. One line under the version, a pure rule (`worldFit` in `zero/content/WorldInstall`): the file's newest release newer than the target's -> warning "Made for 1.21.4, newer than <instance> (1.21.1): the game may not open it."; older -> "Made for 1.20.1. Minecraft upgrades it the first time you open it."; the same, or unknown on either side (snapshots are not ordered by `Versions`) -> no note. A file with **no download URL** (the author turned third-party downloads off): the line says "This world can only be downloaded on CurseForge." and the primary becomes **Open on CurseForge** (`webUrl`: the project's page, http / https only; it opens the browser and closes the dialog, nothing is added; with no usable address it is disabled). Refused while the target runs ("Close <name> to add worlds").
- **Install** (`MainWindow::installWorld`): refuses while the target runs (toast "Close <name> to add worlds", re-checked after the dialog). One `SequentialTask` under a `ProgressDialog` (Skip / abort work): a `NetJob` downloading the file into `<instance root>/.zero/world-download/<file id>-<file name>` (`worldDownloadPath`: a plain file name; CurseForge's hash checked with `ChecksumValidator` when it sent one), then Prism's `worldList()->createInstallWorldTask(zip)` (`InstallWorldTask`: the folder with `level.dat` is extracted into `saves/<unique name>`; it reads the list again itself). The zip is deleted afterwards whatever the outcome (`removeWorldDownload`, also the empty `.zero/world-download` and `.zero` it made), `worldList()->update()` runs, a failure shows Prism's error box. Toast "Added <world> to <instance>" with **Show worlds** -> the ZERO Worlds page with the new world selected (the folder is found by comparing `saves/` before and after: `newWorldFolder`), else the instance window page `worlds`.
- **Top bar**: five segments are wider than four. The global search pill now shrinks down to **200 px** (it was 240) before anything clips; nothing else in the top bar changed (the switcher keeps its natural widths and fixed width).
- **Dev aid**: `ZERO_SCREENSHOT_DISCOVER_SECTION=worlds` with `_INSTALL=1|accept`, `_SELECT`, `_FILTERS`, `_QUERY` as for shaders (the first result is selected on its own with `_INSTALL`; `accept` really downloads and installs into the `-d` data dir and grabs `<file>-after.png` 3 s later). No `_PROVIDER` (CurseForge only). Needs a build with a CurseForge key.
- Tests: `tests/ZeroDiscoverWorlds_test.cpp` (filter core, section, no default chip, not in the cart, pills, version-fit rules, the dialog in world mode, the no-download state, temp path / cleanup / new folder, the fifth segment and the top bar fit at 1100 and 1280 px).

## Discover › Install cart - as built
One cart for the Mods, Resource Packs and Shaders sections (modpacks have none, and neither do worlds: see "Discover › Worlds - as built"): tick items on the cards, switch sections, tick more, and one review adds everything to the target instance, grouped by kind. Part A (core, cards, bar) comes first below; the review dialog and the combined install (Part B) are the last two bullets before Dev aids.
- **Core** (`zero/content/InstallCart`, pure): `CartEntry{kind (Mod | ResourcePack | Shader), item (ContentItemPtr), provider, key "<kind>:<provider>:<project id>"}` in selection order (the same project id in two kinds is two entries); `InstallCart` (`toggle/add/remove/contains/clear`, `count()`, `countOf(kind)`, `entries()`, `entriesOf(kind)`, cap `kCap = 50`: the 51st is `Full`) and the QObject `InstallCartModel` (`changed()` once per real change, `limitReached()` on a refused add). `summaryText()`: "1 mod selected", "3 mods selected", "1 shader selected", "2 resource packs selected", mixed "2 mods, 1 shader selected" (mods, resource packs, shaders; the count picks the noun, no `%n`); `countsText()` is the same without "selected". The page owns the model (`DiscoverPage::cart()`), hands it to the three sections through `ModsEnvironment::cart` (null = no cart: no boxes, no panel toggle), shows "The cart holds up to 50 items" (warning toast) on `limitReached()`. It lives for the session (never saved) and survives searches, filters, section switches and a target change.
- **Eligibility** (`ModsSection::cartEligible`): a target is known and the item is not exactly installed. An installed item is eligible only when its versions are loaded and the newest installable version is not the installed one (the primary action's "Update"); an installed item whose versions are not known yet is not (its primary state is not known either; it becomes eligible once its versions arrive, `ContentSection` repaints the row then). A ticked item can always be unticked.
- **Cards**: a 20 px box (`ContentDelegate::kCheckSize`, radius `radiusSm`), hidden by default, shown on hover, on keyboard focus and on every eligible card while the cart is not empty; ticked = accent fill + `check` in `onAccent` and a 2 px accent card border (`Draw::paintCartCheck`; the box of an empty one is a `bg` scrim with a hairline so it reads on artwork). Position through the public `ContentDelegate::checkRect(cell)` (hit-testing and tests): pack / shader gallery card = 8 px in from the artwork's top-left corner (the provider badge moves right of the box), mod grid card (no artwork) = the TOP-RIGHT corner inside the card padding, vertically centred on the title line (the title leaves room for it whenever the delegate has a cart, so it never jumps), never over the icon; mod list row = left of the icon (the icon and text move right by box + 8 px whenever the delegate has a cart). The delegates ask the section through `CartHooks` (`eligible`, `inCart`, `anyInCart`, `keyboardFocus`); without hooks (Modpacks, a section built without a cart) nothing changes.
- **Interaction** (`ModsSection::eventFilter` on the view and its viewport): a click on the box toggles and does not open the details; Ctrl+click anywhere on a card (not the gap under it) toggles and leaves the open details alone; Space ticks the current card; a plain click opens the details exactly as before; the primary action ("Add to X" / Update) still emits `installRequested` and opens today's single-item dialog. The details panel has a secondary **Add to cart** / **In cart** button (`ContentDetailsPanel::setCartAction`, `cartToggleRequested`) in its own row under the primary / Website row (all three do not fit the 400 px panel), only for eligible or ticked items.
- **Selection bar** (`ui/zero/content/SelectionBar`, built by `DiscoverPage`): 56 px, `elevated`, 1 px `border` top line, padding `spaceLg`, docked edge to edge under the section stack (the page's margins are on an inner body widget, the bar is never over the cards), visible only while the cart is not empty (also on the Modpacks section, where nothing can be ticked), slides up in 160 ms (`Motion::duration`: instant with motion off; the layout gets its 56 px at once, only the surface moves). Content: up to 4 overlapping 26 px item icons and "+N" for more, `summaryText()`, ghost **Clear** (empties the cart), spacer, primary **Review and add to <instance>** (name elided at 160 px; "Review and add" when no section has a target) which only emits `DiscoverPage::cartReviewRequested()`. The instance is `DiscoverPage::cartTargetSection()->target()`: the shown section's when it has one, else the first section with a target; `ModsSection::targetChanged()` keeps the name current.
- **Review dialog** (`ui/zero/content/CartReviewDialog`, 560 px, same look as `ModInstallDialog`; shares `InstallDialogParts`: footer, note row, overline, `PackIconWidget`, and the `DependencySkeleton` that moved there): title "Add 3 mods" / "Add 2 resource packs" / "Add 1 shader" / "Add 5 items" (the cart's counts, also the window title), subtitle "To <instance> · 1.21.1 Fabric" (the loader part only when the cart has mods), a `warning` note "Close <instance> to add content" while the target runs. Groups with overline headers Mods / Resource packs / Shaders (only non-empty, fixed order); the list scrolls past 380 px. A row (48 px): 32 px icon, name (elided), a caption line, a version combo (the target's compatible versions, the single dialog's default: newest release, a beta/alpha is labelled) and a remove X (removes the row from the dialog AND the cart; the last row closes the dialog). Row states: **Loading** (spinner + "Finding versions…", the footer says "Checking…"), **Ready** ("by <author>", or `info` "Update → 2.0" when the target has an older file of it; the text follows the combo), **NoVersion** (`warning` "No version for 1.21.1 Fabric · will be skipped"), **Failed** (`danger` "Couldn't load versions" + Retry, a new generation token), **NeedsModded** (`warning` "Needs a modded instance · will be skipped", no request). Skipped rows stay in the cart. Versions come from the item when its panel loaded them, else from the section's source (`CartReviewEnvironment::sourceFor`) with one generation token per row (`kGenerationBase + n`; failures carry only the generation).
- **Dependencies of the whole cart**: `DependencyResolver::Request` gained `also` (more mod versions in the same run; `all()` = `pack` + `also`) and `InstanceDependencyResolver` hands them ALL to ONE `GetModDependenciesTask` (it merges the dependencies, skips the ones that are themselves selected and lists every mod that needs one). The run starts once no row is loading and again 300 ms after any version change or removal of a mod (`CartReviewEnvironment::dependencyDelayMs`; two quick changes make one run), tokens drop stale answers, the resolver is a child of the dialog and cancelled on close. The answer is merged by `mergeDependencies` (pure, `zero/content/CartInstall`): one entry per provider+project with the union of the "Required by Sodium, Lithium" names, a dependency that is itself a cart row is dropped (it shows once, as the row), `maybeInstalled` ones and the installed required dependencies of the chosen versions go to "Already installed: ...". Same block as the single dialog (skeleton, rows, "No other mods needed", failure + Retry + "Add without dependencies", "Dependency check is turned off in Settings"); hidden while no mod can be added.
- **Footer**: Cancel and the primary, which reads "Checking…" (disabled; Enter and `accept()` do nothing) while versions or the dependency run are pending, then "Add N mods" / "Add N resource packs" / "Add N shaders" / "Add N items" with N = everything that will download, dependencies included (`CartInstallPlan`: mods in cart order, then their dependencies, then packs, then shaders); "Add" (disabled) when nothing will. Resource packs: checkbox **Turn them on** (only for Minecraft 1.13+ targets, checked).
- **Combined install** (`MainWindow::installCart`, from `DiscoverPage::cartReviewRequested`): refuses while the target runs (toast "Close <name> to add content"), re-checks after the dialog; one `ConcurrentTask` of `ResourceDownloadTask`s (same arguments as `installMod` / `installFolderContent`: `loaderModList()` / `resourcePackList()` / `shaderPackList()`, packwiz metadata unless `ModMetadataDisabled`, the reason and `dependentOn` of the plan) under a `ProgressDialog`, Prism's error / warning boxes; each touched model `update()`s. Downloads that succeeded take their entries out of the cart; failed ones (and rows the review skipped) stay. "Turn them on": the new non-update packs go on top in cart order (the first selected is the highest) with ONE `Packs::turnOnManyAtTop` (one `writeOrder`, one history entry, Undo works; refused while the game runs, then the packs are just added). Toast "Added 3 mods and 1 shader to Pack Test" (`cartToastText`: counts of what arrived, mods / resource packs / shaders joined with ", " and " and "; "Updated 2 mods in Pack Test" when every download was an update) with **Show** → the instance window page of the first kind that arrived (`mods`, else `resourcepacks`, else `shaderpacks`). The single-item flows are untouched.
- **Known limit**: the review reads "installed" and "update" facts (and the installed mods for "Already installed") from the section of each kind only when that section's target is the cart's target; if the user picked different instances in two sections, those facts are empty (the dependency check itself always reads the target's real mods folder).
- **Dev aids**: `ZERO_SCREENSHOT_DISCOVER_CART=<rows>` (comma list, 0-based) ticks those rows of the shown section after the first page and its pictures settled (with `ZERO_SCREENSHOT_DISCOVER_SECTION` applied first), then waits for the bar's slide. `ZERO_SCREENSHOT_DISCOVER_CART_REVIEW=1` then presses the bar's primary, grabs the review (instead of the page) once versions, dependencies and pictures settled (20 s at most) and rejects it; `=accept` really adds everything into the `-d` data dir after the grab and grabs the page 3 s later as `<file>-after.png`. Only the shown section can be ticked this way, so a mixed-kind cart is covered by the tests.
- Tests: `tests/ZeroInstallCart_test.cpp` (Part A: core, check box, bar; Part B: texts, plan, merge, pack turn-on on a temp `options.txt`, the dialog with fake sources and resolvers).

## Boost performance - as built
One dialog that makes an instance faster: the curated performance mods for its Minecraft version + loader, the memory the PC can spare and a pointer at a wrong Java. Nothing changes without **Apply**.
- **Entry points**: Library details panel button **Boost performance** (`gauge`, tooltip "Add performance mods and give Minecraft the memory it needs"; hidden while the instance runs, follows the running state through `syncButtons`), and one palette action per instance that is not running, "Boost performance of <instance>" (keywords optimize, fps, memory, ram, sodium, lithium, speed, faster, lag; primary label "Boost"). Both end in `MainWindow::boostPerformance(instanceId)`.
- **Core** (`zero/perf/PerformanceProfile`, pure, `tests/PerformanceProfile_test.cpp`): the curated table lives in ONE place (`allProjects()` + `curatedFor(loader)`): Fabric / Quilt = Sodium, Lithium, FerriteCore, ImmediatelyFast, Entity Culling, ModernFix (Modrinth slugs `sodium`, `lithium`, `ferrite-core`, `immediatelyfast`, `entityculling`, `modernfix`; checked against the Modrinth API in session 11); NeoForge = Sodium (falls back to `embeddium` when Sodium has no NeoForge file for that Minecraft version), FerriteCore, ModernFix, Entity Culling, ImmediatelyFast; Forge = Embeddium, FerriteCore, ModernFix, Entity Culling. `loaderOf(flags)`: NeoForge, Forge, Quilt, Fabric (a Quilt instance reports Fabric too). Row state (`buildRows`): **Installed** (by Modrinth project id from the packwiz metadata, else by mod id of the jar), **Installed (Embeddium)** (the other renderer is there: Sodium <-> Embeddium), **Unavailable** ("Not available for 1.21.1": no file for this version + loader), **ConflictOptiFine** (OptiFine / OptiFabric among the enabled mods: renderer rows are unticked and cannot be ticked, warning pill "Conflicts with OptiFine"), else **Offered** (ticked). The preselected version is `defaultModVersionIndex` of `compatibleVersions` (newest release).
- **Memory rule** (`recommendedMaxMb`): base 3 GB vanilla / 4 GB modded, +1 GB per STARTED 75 mods (count = enabled installed mods + ticked mods; the dependencies are not counted), at most 8 GB, never above half of the PC's RAM (floored to whole GB), at least 2 GB; when half the RAM is below 2 GB the answer is that half (whole GB, at least 1 GB); unknown RAM = no memory section. Min heap = min(1 GB, max / 2). Current value = the instance's own `MaxMemAlloc` when `OverrideMemory`, else the launcher's. **It never lowers**: when current >= recommended the section reads "Memory is fine (6 GB)" with no toggle; otherwise "Give Minecraft 6 GB" + "now 2 GB" with the box ticked by default (until the user touches it).
- **Catalog** (`zero/perf/PerformanceCatalog`): the async seam the dialog asks (`load(token, {slugs, mcVersion, loaders})` -> `loaded(token, FoundMap)` / `failed(token, reason)`, stale tokens dropped; tests derive fakes). `ModrinthPerformanceCatalog` is the production one: `ModrinthAPI::getProjectTask(slug)` for every slug (name, icon, the real project id), then the versions through the same `ModrinthSource(ResourceType::Mod)` the Discover section uses, filtered by the instance's Minecraft version + loaders; 30 s overall timeout; a failure of any request fails the lookup (inline error + Retry, never a modal box).
- **Dialog** (`ui/zero/perf/PerformanceDialog`, 560 px, `InstallDialogParts` footer / note / overline / icons / `DependencySkeleton`): title "Boost performance · <instance>"; sections MODS · FABRIC 1.21.1 (loading = six skeleton rows, error = "Couldn't load performance mods from Modrinth..." + Retry, ready = rows of 48 px: check box, 32 px icon, name, one-line benefit, state pill; a click on the row flips the box; vanilla = the one line "Performance mods need a mod loader such as Fabric. Make a Fabric copy from Library › Copy."; a managed modpack adds the note "This is a modpack: a pack update may remove mods you add."), MEMORY (only once the mods folder is read and, with a loader, Modrinth answered or failed; "Your PC has 16 GB of RAM."), JAVA (only when Java Doctor's `diagnose` says Warning / WillFail: "Java 17 is set; this version needs Java 21." + **Fix**, which closes the dialog and `MainWindow` opens Tools › Java Doctor with the instance as the preferred one; Java Doctor lists problem rows first), footer = summary ("Adds 4 mods (+1 they need) · Memory 2 → 6 GB", "Checking…" while loading, "Nothing to change"), Cancel, **Apply** (primary). The body scrolls past 520 px.
- **Dependencies**: ONE `DependencyResolver` run (`Request::also` = every ticked mod) 300 ms after the last tick (`PerformanceEnvironment::dependencyDelayMs`), generation token drops stale answers, merged with `mergeDependencies` (a dependency that is itself ticked counts once); "Also adds Fabric API, which they need" / "Checking what these need…" / "Couldn't check what these need" + Retry. Apply stays disabled until the run finished (and after a failure until Retry succeeds or the mods are unticked). "ModDependenciesDisabled" = no run, no extra mods.
- **Apply** (`PerformanceDialog::accept`): refused when the instance runs (the dialog closes, `Outcome::refusedRunning`, toast "Close <instance> first"); else the install seam gets `buildCartPlan` of the ticked mods + dependencies; the production seam is `MainWindow::runInstallPlan` (the download half of `installCart`, now shared: one `ConcurrentTask` of `ResourceDownloadTask`s under a `ProgressDialog`, Prism's error / warning boxes, folders read again; `installCart` uses it too, behaviour unchanged). Then, only when the memory toggle is on and the download step did not fail as a whole (nothing arrived), `OverrideMemory=true`, `MaxMemAlloc`, `MinMemAlloc` are written to the instance. A whole failure keeps the dialog open for another try. Toast "Boosted <instance>" with **Show mods** (only when mods arrived) → instance window page `mods`.
- **Seams** (`PerformanceEnvironment`, tests build it without APPLICATION / network): `makeCatalog`, `makeResolver`, `dependencyCheckEnabled`, `loadInstalled` (production: the instance's `loaderModList()`, waits for `updateFinished` and the parse tasks, 6 s at most), `maxMemoryMb`, `checkJava` (production: `JavaGather` = Java Doctor's own gathering + `javalist()->getLoadTask`), `isRunning`, `install`, `writeMemory`. `PerformanceEnvironment::fromApplication(instanceId, install)` is the real one.
- **Dev aid**: `ZERO_SCREENSHOT_BOOST=<instance id>` opens the dialog for that instance, waits until Modrinth, the mods folder, Java and the dependency run answered (20 s at most) and saves the dialog over the dimmed window to `<file>` and the dialog alone to `<file>-dialog.png`; `ZERO_SCREENSHOT_BOOST_APPLY=1` then presses Apply (into the `-d` data dir) and grabs the window 3 s later as `<file>-after.png`. A fake Fabric instance for it needs only `instance.cfg` (with `[General]`) and `mmc-pack.json` (Minecraft 1.21.1 + `net.fabricmc.fabric-loader`); with two fake instances the debug ASSERT in `Task.cpp:108` can stop the app, so use one.
- Tests: `tests/PerformanceProfile_test.cpp` (table per loader, NeoForge fallback, every row state, memory rule on 4 / 8 / 16 / 32 GB PCs x 0 / 50 / 200 mods x vanilla / modded, never lowers, Java line, summary texts) and `tests/ZeroPerformanceDialog_test.cpp` (vanilla, all installed, OptiFine conflict, unavailable row, loading → error → retry with stale answers dropped, the dependency run, memory toggle, Java Fix, Apply wiring, running instance refused, failed download writes nothing).

## Discover page (Modpacks section) - as built
Binding spec: `docs/zero/CONTENT.md` ("Discover page", "### Modpacks", "ZERO Filter System", "Remote images", "Motion"). Rail item **Discover** (`sparkles`, Alt+3), `ui/zero/pages/DiscoverPage`. Top bar: title "Discover", subtitle = the section's results caption. The Modpacks section is described here; the Mods section and the segmented switcher in the section above (Resource Packs and Shaders: see their own sections); `DiscoverPage::showSection(section, provider)` / `MainWindow::showDiscover(section, provider = {})` open it (the palette uses it; `provider` preselects the Provider facet).
- **Provider adapters** (`zero/content/ModpackSource`, `ModpackSources`): `ModpackSource` = async `search(args, generation)`, `details`, `versions`, `changelog`, `requestCategories`; every answer echoes the generation and failures are signals. `ModrinthSource` / `CurseForgeSource` run Prism's `ResourceAPI` (`searchProjectsTask`, `getProjectTask` + `ModPlatform::mergeProjectDetails`, `getProjectVersions`), `TechnicSource` runs `TechnicMeta` + `TechnicDetailsStore`. Pages are 25 rows (`ResultPager`: the offset is the number of provider rows received, a short page ends the list; Prism's own Modrinth model advances by 20 against a limit of 25, which re-lists rows). CurseForge only exists when `Application::SupportsFlame`. The versions request has a 20 s timeout in the source and a 30 s one in the section (the provider's callback never fires on a parse failure).
- **Section** (`ui/zero/content/ModpacksSection`): toolbar, chips, FilterPanel (Provider is a single choice; Technic: search only, its other facets disabled with their reason), featured strip, `ModpackListModel` + `ModpackDelegate` (cards and rows) inside one `QListView` in icon mode, `ContentReveal` (`RevealSpec::modpacks()`), details panel. Rows are dimmed to 50 % while a new search is pending; the "Searching..." spinner appears after 150 ms; the skeleton (Cards) shows only while nothing is loaded; failures are `NetworkErrorState` with Retry; an empty result is `NoMatchState`. Infinite scroll asks for the next page 1.5 viewports before the end (measured from rows and cell size), shows "Loading more..." / "End of results" under the list and "Couldn't load more results." + Retry for a failed page. While the details panel is open the toolbar hides its caption (the top bar has it) and lets the search field shrink to 140 px.
- **Cards** (grid, 280-340 px, columns fill the width) and **rows** (list, 72 px) as in CONTENT.md; the provider badge and "Installed" sit on the artwork corners of a card (not in the footer, which would overflow at 280 px); the footer holds the Minecraft range, up to two loaders and the download count. Technic's list has names and icons only: its first 12 rows get their details in the background (4 at a time) so the cards fill in.
- **Featured strip** (`FeaturedStrip`, `ContentHeroCard`): only on the default view (no query, no chip). "Featured on CurseForge" = CurseForge's Featured sort (`sortField=1`) when the build has CurseForge, else "Popular on Modrinth" = Modrinth's downloads sort. Collapses over 200 ms (height + opacity, instant with motion off). A failed or empty featured request hides the strip (the list below shows its own error).
- **Details** (`ContentDetailsPanel`, reusable; `ContentGalleryViewer` for the picture viewer): see CONTENT.md. Install / Update only emits `ModpacksSection::installRequested(item, updateInstanceId)` (forwarded by `DiscoverPage::installRequested`); the install dialog is wired by the next task. "Update" is offered when an instance manages the pack (`BaseInstance::getManagedPackType/ID`) and its stored version differs from the newest listed one.
- **Settings** (registered in `Application.cpp`): `ZeroDiscoverSection` ("modpacks"), `ZeroDiscoverModpacksFilter` (FilterState JSON without the query), `ZeroDiscoverModpacksView` ("grid" / "list").


## Instance window › Mods page (Mission 003, C5a) - as built
Spec: `docs/zero/CONTENT-MODS.md` "Installed mods page". `ui/zero/instance/ModsPage` replaces Prism's `ModFolderPage` in
`InstancePageProvider::getPages()` (same id `"mods"`; Core/Nil mod pages untouched). Source: `inst->loaderModList()`;
`openedImpl` calls `startWatching()` and `closedImpl` stops it only when this page started it.
- **Layout**: toolbar row (search, filter, Check for updates / Update all, Add mods, `…`), then a 32 px status row: caption or
  selection bar on the left, the "no provider info" line on the right (two rows so everything fits a 1100 px instance window).
- **Toolbar**: search (name, mod id, file name; Ctrl+F, Esc), segmented filter All · Enabled · Disabled · Issues, caption
  "8 mods · 1 disabled" (replaced by the selection bar "N selected · Enable · Disable · Remove" with ≥ 2 rows), **Add mods**
  (→ `MainWindow::showDiscoverMods(instanceId)`: Discover › Mods with this instance as target), `…` menu: Open mods folder,
  Open the classic mods page (Prism's `ModFolderPage` in a plain dialog).
- **Rows** (`ModsList`: row model + painted 56 px delegate + view): toggle switch, icon, name + version, pills (provider or
  "Local file", environment, "Not for <mc>" warning from `issues()`, "Required by N", "Disabled"), `…` row menu (Enable/Disable,
  Open website, Show in folder, Remove). Multi-select, Up/Down, Space toggles, Delete removes.
- **Enable/disable**: the BASE `ResourceFolderModel::setResourceEnabled` (no Prism dependency modal); affected mods outside the
  selection → ZERO question (`ModsDialogs`): "Disable only X" / "Disable all N" / Cancel (enable mirrors it). While the game
  runs: `warning` banner "Close the game to change mods", toggles and Remove disabled; the running state is re-read after every
  dialog so Prism's "game is running" modal is never reached.
- **Remove**: ZERO confirm (up to 8 names + "and N more", "They go to the Recycle Bin.") → `FS::trash` per file (injectable
  `ModsPageEnvironment::trash`), packwiz metadata dropped (`deleteMetadata`) only for files that went; a failure keeps file and
  metadata ("Couldn't move N mods to the Recycle Bin"). Never `deleteResources` (its permanent-delete fallback).
- **States**: skeleton rows on the first load; "No mods yet" + Add mods; "This instance has no mod loader" (no Add).
- **Updates (C5b)**: secondary **Check for updates** (`refresh-cw`) left of Add mods; hidden while the game runs (the running lock
  wins; a check in progress is aborted when the game starts). The check (`ModsUpdates`, default `PrismModUpdateCheck`) runs Prism's
  `ModrinthCheckUpdate` + `FlameCheckUpdate` headless in one `SequentialTask` over the mods that have a packwiz entry with a usable
  project id and are not update-locked (CurseForge only when `Application::SupportsFlame`); inputs as the classic page: the
  instance's Minecraft version, `PackProfile::getModLoadersList()`, the `ModUpdateReleaseTypes` setting. Never
  `ResourceUpdateDialog`, `ensureMetadata` or a message box. While it runs the button reads "Checking…" (disabled), the caption
  "Checking 41 mods…", and the rows are locked like under the running lock (switches dimmed, Remove / `…` changes / selection bar /
  Space / Delete ignored) but with no banner. After it: N > 0 → the button reads "Update all N" ("Update 1 mod"), the filter gains
  **Updates**, the caption reads "8 mods · 3 updates", the `…` menu gains "Check for updates again", each row gets an accent pill
  "Update → 0.6.14" (right-most, elided to 140 px; the row tooltip names the type Release / Beta / Alpha) and its `…` menu starts
  with "Update to 0.6.14"; the selection bar gains "Update" when a selected row has one. 0 updates → toast "All mods are up to
  date". An info line in the status row ("7 mods have no provider info and can't be checked.") shows after a check while mods
  could not be checked. A mod the checker could not answer for gets "Couldn't check for updates: <reason>" in its tooltip; when
  every checked mod failed: error toast "Couldn't check for updates: <first reason>" and no Updates segment.
  Update (one / selection / all): no review dialog, refused while the game runs; the `Update::download` tasks (Prism's
  `ResourceDownloadTask`, which replaces the old file only after the new one arrived) run in one `ConcurrentTask` under a
  `ProgressDialog` (`applyUpdatesWithPrism`, same pattern and error boxes as `MainWindow::installMod`), then `update()`;
  toast "Updated Sodium to 0.6.14" / "Updated 3 mods". The attempted entries leave the results (a download task is used up);
  check again to retry. Results live in the page only, per mod (file name without `.disabled`) + packwiz hash + file size/time:
  a reload keeps them until the file or its metadata changed. Safety: the check keeps the checked `Resource`s alive and the lists
  the checkers hold by reference live on the heap as long as any of its tasks; the page aborts it before a checked mod can go
  (rows about to be removed, model reset / layout change, a swapped resource seen in `dataChanged`, page closed or destroyed, game
  started) and a generation token ignores whatever an aborted check still says.
- Pure core: `zero/mods/InstalledModsModel` (rows, search/filter, captions, pills, the affected decision, remove texts, the update
  results: which entry still fits which row, matching an `Update` to its mod, all the texts of the check).
- Dev aid: `ZERO_SCREENSHOT_INSTANCE_PAGE=<instance id>:<page id>` (with `--zero-screenshot <png> -d <dir>`) grabs that instance
  window page; for `mods`: `ZERO_SCREENSHOT_MODS_SEARCH`, `ZERO_SCREENSHOT_MODS_FILTER=<all|enabled|disabled|issues|updates>`,
  `ZERO_SCREENSHOT_MODS_SELECT=<n>`; `ZERO_SCREENSHOT_MODS_CHECK=1` runs a real update check first (network) and grabs when it settled
  (25 s at most; combine with `..._FILTER=updates`); `ZERO_SCREENSHOT_RUNNING=<id>` shows the banner.
- Tests: `tests/ZeroModsPage_test.cpp` (pure core + the page on a real `ModFolderModel` in a temp folder with a fake trash, a fake
  update check and a fake applier injected through `ModsPageEnvironment::checkUpdates` / `applyUpdates`).
- **Safe update (6a)**: every update applied from the page (row Update, selection Update, "Update all N", the row menu) goes through
  `ModsPage::applyKeys`, which first takes a snapshot and only then calls `applyUpdates`. Core `zero/mods/ModSnapshot` (pure, no widgets).
  - *Snapshot* (`createSnapshot`): the files of the mods being updated, found through the page's own model (the mod's file, enabled or
    `.disabled`, and its packwiz entry `.index/<slug>.pw.toml` from `metadata()->slug`; never guessed from file names), are copied to
    `<data>/zero/snapshots/<instance id>/<yyyyMMdd-HHmmss>[-n]/files/<same relative path>` with `snapshot.json` (format 1: instance id, created
    ISO time, kind `mods-update`, files with size + SHA-256, mods with old -> new version and their files, `added`, `outcome`, `rolledBack`,
    `dismissed`). Built in `<name>.part`, every copy written to `.part`, hashed again and renamed, the folder renamed last. If anything
    fails (a mod not found, disk space, copy error) NOTHING is updated and an error toast says "Could not save a copy of your mods first, so
    nothing was updated: <reason>". It runs on a pool thread under a nested event loop without user input (the page is locked by `m_applying`,
    the same as during the update): the copies are only the replaced files, but a big "Update all" must not freeze the window.
  - *Finalize* (`finalizeSnapshot`, after `applyUpdates` returns, ok or not): lists the folder again (top-level files + `.index/`);
    `added` = after minus before; each mod gets `changed` when one of its files is gone or its content differs (so a packwiz entry rewritten
    under the same name counts); `outcome` ok / failed. An update that changed nothing and added nothing removes its snapshot folder.
  - *Roll back* (`rollback`): refuses while the game runs or a check / update runs (the page's own rule) and for a snapshot already rolled
    back; verifies the hash of every saved copy (a damaged one refuses, naming it, nothing touched); stages copies in
    `mods/.zero-rollback-<n>/` and verifies them; then moves to the Recycle Bin (`FS::trash`, no permanent-delete fallback) every `added`
    file that still exists and every current file at a path about to be restored that is not already the saved version (never overwritten in
    place); then renames the staged files into place (jars first, then entries); removes the staging folder; marks `rolledBack: true`.
    The first Recycle Bin or rename failure stops everything, keeps the snapshot and reports the state of the folder in a dialog; it is safe to
    click Roll back again (a file already gone is skipped, a free path is restored). Files the user changed after the update are skipped
    silently when gone. Prism reloads the folder by itself (hidden `.zero-rollback-*` folders are not listed by the mods model).
  - *Retention*: the 5 newest snapshots per instance are kept (`applyRetention`, after every creation); only folders named like a snapshot
    are ever removed; other instances' snapshots (also of instances that no longer exist) are never touched.
  - *UI*: after an update that changed files a toast "Updated N mods" with **Undo** (also for a partial update, warning tone); a slim `info`
    strip (`ModSnapshotNotice`) above the list while the newest snapshot of the instance is finished, not rolled back and not dismissed:
    "Updated 3 mods · 2 h ago" (relative time refreshed every minute, full date as tooltip), secondary **Roll back**, dismiss x (writes
    `dismissed: true`, the snapshot stays for the retention). While the game runs (or a check / update runs) the strip stays and Roll back
    is disabled with the tooltip "Close the game first" (like the other controls of a locked page). Roll back asks "Roll back 3 mods to the
    versions you had before?" with one line per mod "Sodium   0.5.8 <- 0.6.0" and "The updated files go to the Recycle Bin."; success is a
    toast "Rolled back 3 mods", a failure a dialog with what happened.
  - *Seams*: `ModsPageEnvironment::snapshotsDir` (empty = no snapshots, used by the older page tests) and `snapshotHooks` (`trashPath`
    defaults to `ModsPageEnvironment::trash`). Tests: `tests/ModSnapshot_test.cpp` (core) and the "safe update" cases of
    `tests/ZeroModsPage_test.cpp`.
  - Dev aid: the strip shows by itself when the data dir has a snapshot for the instance (`<data>/zero/snapshots/<instance id>/<stamp>/`);
    `ZERO_SCREENSHOT_MODS_SNAPSHOT=dialog` (with `ZERO_SCREENSHOT_INSTANCE_PAGE=<id>:mods`) grabs the "Roll back N mods?" dialog alone to
    the output file and rejects it.
- **Dependency Inspector (C6)**: spec `docs/zero/CONTENT-MODS.md` "Dependency Inspector". No network; ZERO's own jar analysis.
  - *Core* `zero/mods/DependencyInspector` (pure): per mod file `needs` (mandatory dependencies minus environment ids), `game`
    (Minecraft, the loader, Java: Ok / Mismatch / Unknown, Java is always Unknown), `optional`, `conflicts` (breaks + conflicts, and
    the mods that declare a conflict with this one), `neededBy`, `readable` / `loads`. Dependency states: Installed (version),
    Bundled (inside which file), Disabled (which file), WrongVersion, Missing, Unknown (rule or version not understood, or an
    unreadable jar might provide it), NotInstalled (optional / conflicts). The rules are Mod Health's: both go through the
    internal `zero/mods/ModResolver` (which jars load, provider index incl. nested jars and `provides`, environment versions,
    predicate matching, Quilt `unless`, Forge `side`), so Mods page and Tools > Mod Health never disagree. Lists are sorted by name.
  - *Scan* `zero/mods/DependencyScanner`: runs on the global thread pool after every reload of the mods folder (the page starts it
    300 ms after `ResourceFolderModel::updateFinished`, re-armed by each reload); a generation token drops every answer but the
    newest; `ParseCache` keeps the parsed jars by path + size + modification time (only changed jars are read again). Read-only.
    Injectable: `ModsPageEnvironment::scanRunner` (tests answer scans by hand), `context`, `findMod`, `scanDelayMs`.
  - *Row pills* (before provider / environment / Required by, which drop first; these and the update pill never drop, a row that
    is still too narrow elides them): Missing → danger "Needs Indium" ("Needs Indium +2"); Disabled → warning "Needs Sodium ·
    disabled"; WrongVersion → warning "Wrong Sodium version"; an installed, enabled conflict → danger "Conflicts with OptiFabric".
    Enabled mods only, and only while the scan read the very file of the row (size/time stamp). Such rows are in **Issues** and
    in the caption: "48 mods · 3 disabled · 2 need attention" ("1 needs attention"), before "· N updates".
  - *Panel* (`ModDependencyPanel`, 320 px, `surface` card to the right of the list; the list rows get narrower and drop pills):
    opens on a plain click on a row body or Enter (not on the switch / `…`), follows a single selection while open, closes on ×,
    Esc, a Ctrl / Shift multi-selection or when the mod disappears. Header: logo, name, "version · file name" (tooltip = path).
    Sections Needs / Game / Optional / Needed by / Conflicts (empty ones hidden except Needs: "Needs no other mods." and Needed by:
    "No installed mod needs X."); state pills as in the spec; **Enable** (a Disabled dependency) is the page's normal enable flow
    (affected-mods question, running lock: the button is dimmed while the game runs); **Find** (a Missing one) →
    `MainWindow::showDiscoverMods(instanceId, query)` (Discover › Mods, this instance, the dependency id as the search text); a
    Needed by row (the scan joined with Prism's `requiredByList`) selects that mod and shows its panel (a filter that hides it is
    lifted). Unreadable jar: "ZERO couldn't read this mod's details." + Needed by. Until the scan answered: 4 skeleton lines.
  - Dev aid: `ZERO_SCREENSHOT_MODS_INSPECT=<mod name>` opens that mod's panel before the grab, once the scan answered (8 s at most).
  - Tests: `tests/DependencyInspector_test.cpp` (real zip jars: every state, game entries, optional, conflicts both ways, Needed by,
    Quilt `unless`, Forge `side`, agreement with `ModHealth::analyze`, parse cache, scanner generation) and the new cases of
    `tests/ZeroModsPage_test.cpp` (pills / Issues / caption from an injected scan, panel open / close rules, Enable / Find / Needed
    by, skeleton, stale answers, a real pool scan following the folder).

## Instance window › Resource packs page (Mission 003, D2) - as built
Spec: `docs/zero/CONTENT-PACKS.md` "D2 Installed resource packs". `ui/zero/instance/ResourcePacksPage` replaces Prism's
`ResourcePackPage` in `InstancePageProvider::getPages()` (same id `"resourcepacks"`, same `shouldDisplay`; texture/data pack pages
untouched). Source: `inst->resourcePackList()` (watched only while opened, stopped only when this page started it) plus
`<gameRoot>/options.txt`, read again on show, on folder reload and when the game stops.
- **Order core** `zero/content/PackOrder` (pure, no instance): edits `options.txt` as bytes (only the `resourcePacks` line changes;
  LF/CRLF, BOM and a missing final newline survive), non-`file/` entries (`vanilla`, `fabric`, mod ids) keep their slots, new packs
  are appended (= top), entries of missing files are left alone, an unreadable value (pre-1.13 format) is never rewritten. Writes go
  through `QSaveFile`; before each write the old `resourcePacks` / `incompatibleResourcePacks` lines are saved to
  `<instance root>/.zero/pack-order.json` (newest first, cap 20). `undoLast(expectedId)` restores them. Running is asked before
  reading and again right before writing. `supportsOrdering(mc)`: 1.13+ / 17w43a+ (older: a note, no toggles).
- **Rows** `zero/content/InstalledPacks` (pure: `PackRow`, `plainDescription` strips HTML / `§` codes, `packFit` → Fits /
  "Made for 1.20" / Unknown from `ResourcePack::mappings()`, filters, captions, pills) + `PackList` (`PackListModel`, `PackDelegate`,
  `PackListView` with drag reorder inside On). Groups: **On — top wins** (fixed "Default resources" last row), **Available**,
  hidden `.zip.disabled` files last with **Unhide** (base `setResourceEnabled`, a rename only).
- **Actions**: toggle (on = top), drag / Alt+Up/Down / row menu (Top, Up, Down, Bottom), Undo in every toast, Remove → Recycle Bin
  through the shared `ModsUi::moveToRecycleBin` (no permanent fallback; metadata deleted only for a trashed file; the entry leaves
  `resourcePacks`), overflow: Open folder, Restore previous order, classic Prism page in a dialog. Running: shared `RunningBanner`
  "Close the game to change packs", everything refused, state re-read after every question dialog.
- **Discover add dialog**: "Turn it on" (checked, 1.13+ targets, not for updates) → `Packs::turnOnAtTop` after a successful download;
  toast "Added <name> to <instance> and turned it on".
- Dev aid: `ZERO_SCREENSHOT_INSTANCE_PAGE=<id>:resourcepacks` waits until the packs are listed (8 s max). Tests `PackOrder`,
  `ZeroResourcePacksPage`.
