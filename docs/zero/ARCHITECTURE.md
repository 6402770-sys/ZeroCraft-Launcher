# ZERO Architecture

ZERO is Prism Launcher's engine with a new product layer on top. Prism's core (instances, Minecraft
install/launch, auth, mod platforms, tasks, networking) is kept and only changed where ZERO needs it.

## Layers
| Layer | Where | Rule |
|---|---|---|
| Prism core | `launcher/` (minecraft, modplatform, net, tasks, java, settings, …) | Behaviour preserved. Change only with tests. |
| ZERO services (non-UI) | `launcher/zero/` | Pure logic, no widgets, unit-tested (`tests/Zero*_test.cpp`). Diagnostics, profiles, backup, storage go here. |
| ZERO design system | `launcher/ui/zero/` (`ZeroTokens.h`, `ZeroStyle`, `ZeroTheme`, `ZeroIcons`, components) | All ZERO UI is built from these. `docs/zero/DESIGN.md`. |
| ZERO shell + pages | `launcher/ui/zero/shell/`, `launcher/ui/zero/pages/` | Presentation only. Trigger existing actions/APIs, never re-implement launcher logic. `docs/zero/SHELL.md`. |
| Legacy Prism UI | `launcher/ui/pages`, `launcher/ui/dialogs`, … | Still used for settings, instance editing, mod browsing; styled by the Obsidian stylesheet. Replaced page by page. |

## Key decisions
- **MainWindow is the controller.** It owns QActions, selection, launch/kill and dialogs; its view is `ZeroShell`.
  This keeps every battle-tested Prism flow (launch, delete with undo, groups, drag-and-drop, imports) working while
  the UI is replaced.
- **Theme = runtime `Zero::ThemeSpec`.** The stylesheet is generated from the spec (`launcher/resources/zero/obsidian.qss`
  with `@token@` placeholders), so Theme Studio can change accent/density without touching widgets.
- **Identity separation.** Own name, binary, AppID, data dir (`%APPDATA%\ZEROLauncher`), config (`zerolauncher.cfg`),
  installer keys and URL scheme. Prism data is only ever *copied* on first run after explicit consent
  (`Application::handleDataMigration`), absolute paths into the old install are rebased.
- **Shared upstream infrastructure** (meta server, Prism MSA client ID, CurseForge key) is reused for dev builds only;
  `-DZERO_RELEASE_BUILD=ON` refuses to build with upstream credentials. See `docs/zero/CREDENTIALS.md`.
- **No mock UI.** Navigation entries appear only when their feature works.

## Build
MSYS2 UCRT64 (GCC, Qt 6.12, CMake, Ninja) — `scripts/zero/build.sh`. Helper Java jars target `--release 8`.
The vcpkg/MSVC preset from upstream is not used for ZERO dev builds.

## Instance profiles (Mission 002, milestone 6)
- **Data**: everything lives with the instance, no parallel store. `BaseInstance` registers `ZeroFavorite` (bool), `ZeroTags` (JSON array in a
  string; the INI format quotes it because of the commas) and `ZeroArtwork` (path relative to the instance root) in `instance.cfg`; the
  description is Prism `notes`. Artwork is `<instance root>/.zero/artwork.png` (scaled to at most 1600 px on the long side), written
  through a temporary file that replaces the old artwork only when completely written (`QSaveFile`), never outside the instance folder.
- **Code**: `launcher/zero/profiles/InstanceProfile` (setters / getters, tag normalization, artwork, mod count, `Notifier::profileChanged` /
  `artworkChanged`) and `LibraryFilter` (chips and search as pure logic over `Facts`). Setters save, emit `BaseInstance::propertiesChanged` (so
  `InstanceList` and its proxies re-filter / re-sort) and the notifier. UI: `FilterChipBar`, `ArtworkCache` (cover-cropped pixmap cache, dropped on
  `artworkChanged`) / `ArtworkView`, `FlowLayout`, the card delegate, the details panel and Home read these.
- **Filtering** runs in `InstanceProxyModel` (text filter + `setPredicate` hook), so the grid, the list and Prism groups (categories = groups) agree.
- Out of scope for now: bulk actions (multi-select tag / favorite / move), sorting by "recently added", a favorite column in the list view.

## Additions in session 2 (Mission 002)
- **Themes**: `Zero::ThemeSpec` v2; one `ITheme` per spec (`ZeroTheme` = `zero-obsidian`, `ZeroMinecraftTheme` =
  `zero-minecraft`) registered first in `ThemeManager`; stylesheet = `obsidian.qss` + optional `<id>.qss` overlay.
  Painted panels go through `ZeroPaint` (corner style, bevel, texture). `ZeroPixelFont` builds an original TrueType
  font in memory.
- **Motion**: `ZeroMotion` (tokens, easings, setting resolution incl. Windows animation flag), `ZeroTransition`
  (mouse-transparent snapshot overlays; widgets never animate their layout), `ZeroRipple` (app-wide press filter).
  `ThemeManager::setApplicationTheme` snapshots windows before applying and dissolves/crossfades after.
- **Launch state**: `Application::launchPhase()/launchPhaseChanged/launchEnded` (Preparing when the controller starts,
  Running when `LauncherPartLaunch` sees the process run). UI reads these; launch behaviour unchanged.
- **Shell v2**: `ZeroShell` = `[ZeroNavRail | ZeroTopBar over page stack]`; pages derive from `ShellPage`
  (title/subtitle/breadcrumb/top-bar actions). MainWindow remains the controller.
- **Command palette**: ranking is pure logic in `launcher/zero/palette/CommandMatcher` (unit-tested); UI in
  `launcher/ui/zero/palette/`; MainWindow supplies command descriptors that call existing QActions/APIs.
