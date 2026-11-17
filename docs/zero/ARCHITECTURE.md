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
- **Two languages: English and Español** (see "Languages" at the end).
- **Default theme** = `Zero::kDefaultThemeId` (Minecraft). `Zero::appLogo()` / `Application::logo()` return the
  pixel mark under the Minecraft theme; `ThemeManager::setApplicationTheme` sets it as the application window icon on
  every switch (the main window has no own icon, so it follows).
- **Motion v2.** `ZeroMotion` empty = on. Page enter = `Transition::Kind::Cascade` (old snapshot washes to `bg`, new
  snapshot rises 10 px and is revealed top to bottom through a 180 px gradient mask). `Motion::ItemReveal`
  (`ui/zero/ZeroReveal`) wraps a view's delegate (`installItemReveal`) and paints items with a stagger by position
  (`durStagger` per 56 px down / 220 px right, capped at 420 ms). `ZeroSkeleton` = loading placeholder with a sweep that
  only runs while visible with motion on. `ThumbnailCache` remembers arrival times for the fade-in.
- **Favorites** (`zero/favorites/FavoriteStore`): one JSON file per data dir, keys per kind (world =
  `<instance>/<folder>`, server = normalized address), `changed(kind, key, favorite)`; `FavoriteStore::instance()` is
  null without an Application (tests), and the UI then hides every star.
- **Page frame.** Prism's `PageContainer` (settings, instance window, page dialogs) keeps its API but builds a ZERO
  layout: `Zero::PageNavDelegate` side list (`kPageIdRole` from `PageModel`), title + `PageHelp::forPage().about`, and
  with `setOptionHelpEnabled(true)` an app-level event filter that maps Enter/FocusIn inside the current page to
  `PageHelp::forWidget()` (tooltip, else ZERO text by object name, else by enclosing group box). Sections switch with
  the Cascade overlay. `setSidebarHeader()` / `setSidebarFooter()` for the instance card and the "saved when you leave"
  note.
- **Settings page.** `Zero::ZeroSettingsPage` creates a `PageContainer` from `Application::globalSettingsPages()` on show
  and applies + deletes it on hide (fresh values every time; a second, modal settings dialog can never be overwritten
  by stale widgets). `Application::OpenSettings()` navigates there; `ShowGlobalSettings()` stays the modal dialog for
  flows that must block.
- **Accessibility of painted widgets.** `ZeroCard` gets a factory interface (clickable = Button with a "press" action that
  emits `clicked()`, name falls back to its first label); `PaletteResults` exposes one ListItem per row (StaticText for
  headers, "press" runs the command) whose interfaces are invalidated by a generation counter on every `setRows()`.

## Build
MSYS2 UCRT64 (GCC, Qt 6.12, CMake, Ninja) — `scripts/zero/build.sh`. Helper Java jars target `--release 8`.
The vcpkg/MSVC preset from upstream is not used for ZERO dev builds.

## Packaging (portable zip, installer, updater)
`scripts/zero/package.sh [--version X.Y.Z] [--preview|--release] [--no-installer] [--no-smoke]` (Git Bash, repo root).
- **Steps**: configures a separate Release tree in `build-release/` (Ninja, ccache, no LTO, no tests, `-DZERO_VERSION=...`), builds, `cmake --install --strip` into `dist/stage/ZEROLauncher/`,
  then makes the folder self-contained: Prism's install-time deploy (`qt_generate_deploy_script`, i.e. windeployqt) ships the Qt plugins, then the script walks the imports of every exe / DLL / plugin
  with `objdump -p` and copies each non-system DLL from `C:\msys64\ucrt64\bin` (libstdc++, libgcc, winpthread, zlib, ssl, ffmpeg and their dependencies, about 75 files). Missing plugins trigger a
  `windeployqt6` fallback; an import that cannot be resolved fails the script. `Qt6Multimedia.dll`, `multimedia/ffmpegmediaplugin.dll` and the ffmpeg DLLs are checked explicitly (video themes), as are
  `platforms/qwindows`, `styles`, `tls/qschannelbackend` (HTTPS) and the `imageformats` plugins (jpeg, gif, svg, webp; png is built into QtGui).
- **Outputs** (`dist/`, git-ignored): `ZEROLauncher-<ver>-Windows-Portable.zip` (flat, stage + `portable.txt`), `ZEROLauncher-<ver>-Windows-Setup.exe` (only when `makensis` is found; otherwise the script
  prints the MSYS2 install line `pacman -S --needed mingw-w64-ucrt-x86_64-nsis`), `SHA256SUMS.txt`. The installer is built from `build-release/program_info/win_install.nsi` (configured from
  `program_info/win_install.nsi.in`); the script copies it next to the staged files and makes `OutFile` / `MUI_ICON` absolute. Every top-level stage folder must be packed by the template or the script stops.
- **Smoke test** (on by default): extracts the zip to a temp folder and runs it with `PATH=C:\Windows\System32;C:\Windows`, a temp data dir and `--zero-screenshot`; it must exit 0 and write a PNG.
- **Version**: the numbers in the root `CMakeLists.txt` (`Launcher_VERSION_*`); `--version` passes `-DZERO_VERSION=X.Y.Z`, which overrides them for that build tree.
- **Updater**: `Launcher_UPDATER_GITHUB_REPO=https://github.com/6402770-sys/ZeroCraft-Launcher`, `Launcher_BUILD_ARTIFACT=Windows`, `Launcher_BUILD_PLATFORM=Windows-MinGW-w64` (so
  `zerolauncher_updater.exe` is built and shipped). `PrismUpdaterApp::validReleaseArtifacts` accepts a GitHub release asset when its lower-cased name contains the artifact (`windows`), contains
  `portable` for a portable install (the `portable.txt` next to the exe) and is not an archive for an installed one. So upload exactly `ZEROLauncher-<ver>-Windows-Portable.zip` and
  `ZEROLauncher-<ver>-Windows-Setup.exe` to the release (no other asset name may contain "windows": it would be offered as a choice).
- **`--release`** adds `-DZERO_RELEASE_BUILD=ON` and requires the env vars `ZERO_MSA_CLIENT_ID` and `ZERO_CURSEFORGE_API_KEY` (never printed; they land in the git-ignored
  `build-release/CMakeCache.txt`, so do not share that folder). Without them the script stops; see `docs/zero/CREDENTIALS.md`. A `--preview` build still carries Prism's development credentials and
  must not be published.
- **Displayed version**: `BuildConfig::printableVersionString()` appends the git branch (`0.1.0-zero-development`) unless the build is on a commit whose git tag equals the version. For a public release
  tag the commit `X.Y.Z` before running the script, otherwise the package (and the updater's version comparison) carries the branch suffix.
- **Compiler flags**: the upstream `-mguard=cf` Release flag is only added for Clang (GCC rejects it).
- **Installer notes** (per-user install in `%LOCALAPPDATA%\Programs\ZEROLauncher`, registry under `HKCU\Software\ZEROLauncher`): an upgrade runs the previous uninstaller first (program files only; user
  data is never inside the install folder, it lives in `%APPDATA%\ZEROLauncher`); nothing in the template references `%APPDATA%\PrismLauncher`. It registers the `zerolauncher:`, `curseforge:` and
  `modrinth:` URL handlers (the last two are shared with other launchers: the uninstaller removes them only while they still point at ZERO) and optional `.mrpack` / `.zip` "Open with".
  NSIS is now only the fallback (`package.sh --nsis`, or when qt6-static is missing).
- **ZERO Setup v2** (default installer since session 12): own animated Qt installer in `installer/` (standalone CMake project, statically
  linked against MSYS2 `qt6-static`, imports only Windows DLLs). `zero-setup-pack` appends the `dist/stage` payload (per-file SHA-256, zlib)
  to the stub; the stub reproduces the NSIS footprint above exactly (same keys, paths, handlers, `/S` `/D=` switches) and copies itself
  without payload as `uninstall.exe`. Uninstall moves user data to the Recycle Bin only when asked. Details, switches, payload format,
  test switches (`--target`, `--no-system`, `--screenshot`, `--demo`): `installer/README.md`.
- **Shared brand scene** (`launcher/ui/zero/ZeroScene.{h,cpp}` + generated `ZeroMarkData.h`): the night sky with twinkling stars, the stone ground
  with its grass edge, the pixel ZERO mark built from 252 blocks that drop in, and the wordmark are painted by one Qt-only file that both
  `installer/` (`SetupCanvas`, fixed 760 x 460 window; the installer's CMake compiles the three files from the repo root next to `ZeroPixelFont.cpp`) and the launcher's
  first-run intro (`ui/zero/onboarding/IntroOverlay`, scales with the window) use, so the intro continues the installer's look. The file has no Application,
  theme or motion dependency: colours come in as a `Scene::Palette` (the Minecraft palette of `ZeroTokens.h` / `SetupTokens.h`), time as plain
  milliseconds, geometry as arguments. Design lengths (drop distance, bob, wordmark size and spacing) are given for a 6 px mark cell and scaled
  by the caller. `ZeroMarkData.h` is regenerated with `installer/tools/gen_mark.py` (namespace `Zero::Scene::MarkData`). The installer renders byte-identical
  screenshots (welcome, drop, options, installing) before and after the move.

## Instance profiles (Mission 002, milestone 6)
- **Data**: everything lives with the instance, no parallel store. `BaseInstance` registers `ZeroFavorite` (bool), `ZeroTags` (JSON array in a
  string; the INI format quotes it because of the commas) and `ZeroArtwork` (path relative to the instance root) in `instance.cfg`; the
  description is Prism `notes`. Artwork is `<instance root>/.zero/artwork.png` (scaled to at most 1600 px on the long side), written
  through a temporary file that replaces the old artwork only when completely written (`QSaveFile`), never outside the instance folder.
- **Code**: `launcher/zero/profiles/InstanceProfile` (setters / getters, tag normalization, artwork, mod count, `Notifier::profileChanged` /
  `artworkChanged`) and `LibraryFilter` (chips and search as pure logic over `Facts`). Setters save, emit `BaseInstance::propertiesChanged` (so
  `InstanceList` and its proxies re-filter / re-sort) and the notifier. UI: `FilterChipBar`, `ArtworkCache` (cover-cropped pixmap cache, dropped on
  `artworkChanged`) / `ArtworkView`, `FlowLayout`, the card delegate, the details panel and Home read these.
- **Background video** (per instance): setting `ZeroBackgroundVideo` = file name inside `<instance root>/.zero/` (`background.<ext>`, ext = one
  of the theme import types mp4/webm/mkv/mov, limit = the theme import limit 200 MB). `Profile::checkBackgroundVideo` validates, the widget-free
  `copyBackgroundVideo(instanceRoot, source, &name)` copies through a `.part` temp file + rename (the old video is replaced only when the copy
  is complete; an old video with another extension is removed afterwards) and is safe on a worker thread; `commitBackgroundVideo` saves the
  setting and notifies (`Notifier::backgroundVideoChanged`, then `profileChanged`). `setBackgroundVideo` = all three on the calling thread
  (tests); `MainWindow::chooseBackgroundVideoForSelected` runs the copy with `QtConcurrent` + "Copying video..." toast.
  `backgroundVideoPath` is empty for an unset / missing file or any name that is not a plain supported file name. Library details panel: "Set
  background video..." / "Remove video" + caption. Playback: `HomePage::heroVideoChanged(path)` (every refresh; the hero is the last played
  instance) -> `ZeroShell::setPageVideo("home", path)`. `ZeroShell::chooseVideo(page, overridePage, path, motionOff, gameRunning)` (pure) says
  Theme or Page: the instance video is drawn on its own page only, never with reduced motion (theme background shows, nothing decodes); a
  running game only pauses it. A second `ZeroVideoBackground` (`m_pageVideo`, muted) holds a source only while shown (released when Home is
  left); the theme video is paused (conditions page = none) while it is shown, so one decoder plays. Scrim = `max(theme dim, Tokens::pageVideoMinDim)`.
- **Filtering** runs in `InstanceProxyModel` (text filter + `setPredicate` hook), so the grid, the list and Prism groups (categories = groups) agree.
- Out of scope for now: bulk actions (multi-select tag / favorite / move), sorting by "recently added", a favorite column in the list view.

## Session history (Mission 002, milestone 7)
- **Data**: `<data root>/zero/session-history.json`, `{ "version": 1, "sessions": [...] }`, newest first, capped at 500 (oldest dropped). Entry keys:
  `instanceId`, `instanceName`, `started` / `ended` (ms since epoch), `outcome` (`played` / `stopped` / `crashed` / `failed`), `exitCode` (only
  when known). Loaded lazily, saved with `QSaveFile` after every new session. A missing file is an empty history; an unreadable file is renamed to
  `session-history.json.corrupt` (replacing an older one) and never overwritten; malformed entries are skipped.
- **Classification** (`History::classify`, pure): game started + success = Played; started + aborted = Stopped; started + not success = Crashed
  (exit code from `gameStoppedUnexpectedly` when seen); not started + not success and not aborted = Failed; cancelled during preparation records
  nothing. `started` = the `Running` phase (game process up), for Failed the `Preparing` time; `ended` = `launchEnded`.
- **Wiring**: `launcher/zero/history/SessionHistory` is created in `Application` right after the instances load (marked `// ZERO:`) and connected to
  `launchPhaseChanged`, `gameStoppedUnexpectedly` and `launchEnded`, so CLI launches without a main window are recorded too. The core takes an
  injectable clock and name lookup and does not depend on `Application` (own `Phase` enum). Home's "Recent activity" reads `SessionHistory::global()`.

## Screenshot Gallery
- **Scanner** (`launcher/zero/gallery/ScreenshotScanner`, no widgets, no `APPLICATION`): `scanScreenshotsDir(instanceId, instanceName, dir)` lists `*.png|jpg|jpeg` (case-insensitive) directly in `<gameRoot>/screenshots`, skipping links, junctions and non-files; `sortScreenshots` (Newest / Oldest / Instance) and
  `matchesSearch` are pure. `ScreenshotScanner` mirrors `WorldScanner`: the source provider runs on the GUI thread, the scan on `QThreadPool::globalInstance()`, results come back through `QCoreApplication::instance()` (queued) with a generation check, `listChanged()` / `scanningChanged()`. Read-only.
- **Thumbnails** (`ui/zero/pages/ThumbnailCache`): decoded on the pool with `QImageReader::setScaledSize` (never the full image on the GUI thread), `QCache` LRU of 300 pixmaps keyed by path + modified time, `thumbnailReady(path)`; memory only, nothing is written to disk. Undecodable files stay placeholders.
- **UI**: `ScreenshotsPage` (filter / sort, settings `ZeroScreenshotsInstance` / `ZeroScreenshotsSort`), `ScreenshotModel` + `ScreenshotDelegate` (painted tiles in a `QListView` icon mode), `ScreenshotViewer` (overlay child of the page; full image decoded on the pool, the shown file is moved to the Recycle Bin with
  `FS::trash` only after a confirmation and never permanently deleted when that fails). Tests: `tests/ScreenshotScanner_test.cpp`.

## Server Vault (Mission 002)
- **Read-only rule**: `servers.dat` is only ever opened for reading (`QFile` ReadOnly, nothing is written, renamed or locked); adding, editing and removing servers stays in Prism's per-instance Servers page. The only side effect of the page is the quick-join launch.
- **Ping is not availability**: a `Responded` result only means the server answered a Server List Ping (status request). No wording, colour or sort treats it as "online" or "joinable"; every surface shows the disclaimer.
- **Core** (`launcher/zero/servers/ServerVault`, no widgets, no `APPLICATION`): `parseServersDat(path)` (own NBT reader on the bundled nbt lib; uncompressed NBT, root compound with a `servers` list of `name` / `ip` / base64 `icon`; missing file = empty, damaged / wrong shape = empty + error text; entries without an address and `hidden` ones, which Minecraft keeps for direct connect, are skipped), `parseAddress` (host, port default 25565, `[v6]`, `[v6]:port`, bare IPv6, trimmed and lowercased; bad characters, empty labels, port 0 / > 65535 / not digits are invalid), `groupByAddress` (key = normalized address, port shown only when not 25565; distinct names, instances by id, first non-empty icon; sorted by display name), `matchesSearch`, `sortServers` (Name / Players / Latency; responded servers first). `ServerVaultScanner` is a provider + pool + generations scanner like `WorldScanner` (`listChanged()`), and collects `errors()` for broken files.
- **Pinger** (`launcher/zero/servers/ServerPinger`, GUI thread, Qt networking): resolves with Prism's `McResolver` (SRV then A; IP literals skip DNS entirely) and asks with `McClient` (handshake + status request); latency = `QElapsedTimer` from connect to the status response. At most 4 pings in flight, 5 s timeout each, results cached for the session (`ping()` of a known address does nothing; `refresh()` forgets and re-asks; superseded jobs finish quietly and still count against the 4). Pure helpers: `stripFormatting` (`§` + next char), `flattenMotd` (string / chat object with `extra[]` / array; plain text, trimmed lines, <= 512 chars, no control characters), `resultFromStatus`.
- **Privacy**: pings go only to addresses the user saved in their own `servers.dat`; no other network call is made, and only the address and the outcome are logged (Qt debug).
- **UI**: `ui/zero/pages/ServersPage` (toolbar, list, states, settings `ZeroServersInstance` / `ZeroServersSort`), `ServerRows` (`ServerRowModel` + painted `ServerRowDelegate`, `ServerIcon` nearest-neighbour cache), `ServerDetailsPanel`; `PillTag::paintPill` paints the status pills inside the delegate. Tests: `tests/ServerVault_test.cpp` (real NBT files, address table, grouping, scanner, motd helpers, and the pinger against a fake status server on 127.0.0.1).

## Worlds Hub (Mission 002, milestone 8)
- **Read-only rule**: the scanner and the page never write, rename, move or delete anything inside an instance or a world. They do not use `World::rename / destroy / resetIcon / install`
  or touch level.dat; `tests/WorldScanner_test.cpp` hashes a world tree (contents and mtimes) around a scan to keep that true. The only writers are the Smart Backup service and its
  jobs (below); import / export are a later task. The scanner skips folders named `.zero-*` (see "Smart Backup").
- **Scanner** (`launcher/zero/worlds/WorldScanner`, no widgets, no `APPLICATION`): `scanWorldsDir(instanceId, instanceName, savesDir)` reads `<savesDir>/<folder>/level.dat` through
  Prism's `World` (+ ZERO `// ZERO` additions: `versionName()` = `Data.Version.Name`, `isHardcore()` = `Data.hardcore`) and skips zips, loose files, folders without a parsing level.dat;
  `folderSize` is an own traversal that skips symbolic links and junctions (Qt's recursive listing follows junctions). `WorldScanner` takes a source provider (id, name, saves dir), called
  on the GUI thread by `rescan()`; the page injects the instance list. Phase 1 (pool thread) scans the metadata and emits `worldsChanged()` (sizes -1, or the previously known size so
  a refresh does not flicker); phase 2 computes one size per pool job and emits `sizeReady(key, bytes)`. `isScanning()` is true until the last size arrived.
- **Generations**: every `rescan()` increments a counter that the jobs carry back; results of an older generation are dropped, so two quick rescans deliver only the second one. Pool jobs
  only hold a `QPointer` to the scanner and post the result with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`; they never touch `this`, so a scanner that dies while jobs run is safe.
- **UI**: `WorldsPage` (filter / sort / selection by key), `WorldRowModel` + `WorldRowDelegate` (painted rows, no per-row widgets), `WorldDetailsPanel`, `WorldIcon` (nearest-neighbour icon tiles).
  Sorting and search are pure functions in `WorldScanner.h` (`sortWorlds`, `matchesSearch`). Settings: `ZeroWorldsSort`, `ZeroWorldsInstance`.

## Smart Backup (worlds, Mission 002, milestone 9)
The first ZERO feature that writes inside user worlds. Rule: never silently overwrite or lose user data. Code: `launcher/zero/backup/WorldBackup` (pure functions, no widgets, no
`APPLICATION`; seams for tests in `Hooks`: rename, free space, trash) and `WorldBackupJobs` (runner); UI: `WorldBackupsSection` inside the Worlds details panel.
- **Storage**: `<dataRoot>/zero/backups/<instanceId>/<worldFolder>/<yyyyMMdd-HHmmss-zzz>-<manual|safety|auto>.zip` (UTC stamp; a taken name gets `-2`, `-3`...). It is outside the instance,
  so backups survive instance deletion and never end up in instance exports. Written through Prism's `ArchiveWriter` (libarchive zip); read through `ArchiveReader` (own streaming
  `File::readBlocks`, `// ZERO`, so multi-GB region files are never held in memory).
- **Manifest**: first entry `zero-backup.json` = `{version 1, kind, createdMs, instanceId, instanceName, worldFolder, worldName, files:[{path, size, sha256}], totalBytes}`; world files are
  stored as `world/<relative path>`. Links (symbolic or junction, same traversal as `folderSize`) are skipped and `session.lock` of the world root is not stored; empty folders are not
  stored. Files are hashed in 1 MiB chunks before they are added.
- **Verify before rename**: the zip is written as `<name>.zip.part`, then reopened and checked (`verifyBackup`: manifest present and well formed, every manifest file present with the same size and
  SHA-256, no duplicate and no unknown entry); only then it is renamed to `.zip`. On any failure the `.part` is deleted, so a partial file never looks like a backup. `createBackup` refuses
  when the target volume has less than world size x 1.1 + 50 MB free ("Not enough free disk space for a backup (needs about X)").
- **Path safety (zip-slip)**: every archive and manifest path must pass `isSafeRelativePath`: relative, '/'-separated, no empty / `.` / `..` segment, no backslash, no `:` (drive letters, NTFS streams),
  no control or `<>"|?*` characters, no Windows device name (CON, NUL, COM1...), no trailing dot or space; duplicates (case-insensitive) are rejected. It runs in `verifyBackup`, again before every file
  is created during extraction (plus a resolved-path-below-staging check), and for the files of the world being backed up. `MMCZip::extractDir` is not used for restores.
- **Restore order** (`restoreBackup`, stops at the first failure with the original world untouched): (a) `verifyBackup`; (b) if the world exists, a verified `safety` backup of it; (c) extract into
  the sibling `<saves>/.zero-restore-<folder>-<ts>` (same volume, so renames are cheap) and re-hash the staged files from disk against the manifest; (d) rename the world to
  `<saves>/.zero-replaced-<folder>-<ts>`, rename staging to `<folder>`; if the second rename fails the first is rolled back (and if that fails too the error names the `.zero-replaced-*` folder);
  (e) only after success trash the replaced folder (Recycle Bin, fallback delete), first renamed to `<folder> (replaced yyyy-MM-dd HH.mm.ss)` so the Recycle Bin shows which world it was and its "Restore" brings it back as a separate world; if the trash fails it is renamed back to the hidden `.zero-replaced-*` name; a failure there is logged and the restore still succeeds, the old world is also in the safety backup;
  (f) any failure after (c) started deletes the staging folder. Folder renames retry a few times (antivirus / indexer handles on Windows) but never into an existing destination.
- **Leftover folders**: an interrupted restore (crash, power loss) can leave `.zero-restore-*` or `.zero-replaced-*` next to the worlds. They are never auto-deleted (a `.zero-replaced-*` may be the only
  copy of a world), and `scanWorldsDir` ignores every folder starting with `.zero-`, so they do not show up as worlds. The user can delete or rename them by hand.
- **Retention** (`applyRetention`, run by the jobs after every successful backup / restore): keep the 10 newest `manual`, the 3 newest `safety` and the 5 newest `auto` (`kKeepAuto`) per world, each kind independently (an auto backup never evicts a manual or safety one, and vice versa), by the timestamp in the file name (a
  `-N` suffix compares numerically). It never deletes the backup just created or the one just restored (protected list) and only touches `*-manual.zip` / `*-safety.zip` / `*-auto.zip` with the exact name pattern; a damaged
  zip with such a name still counts as one of the kept ones. `deleteBackup` (Delete button) only accepts a `.zip` inside `<dataRoot>/zero/backups` and moves it to the Recycle Bin.
- **Jobs**: `WorldBackupJobs` runs `backup / restore / remove / refreshList` on `QThreadPool::globalInstance()` (same posting pattern as `WorldScanner`: the pool never touches `this`, the QPointer is read on
  the GUI thread). It is an **app-level service**: `Application` creates it (with the running check: `launchPhase != Idle` or `isRunning()`) next to `SessionHistory` in a `// ZERO:` block and
  `WorldBackupJobs::global()` returns it (the first created instance is global, like `SessionHistory::global()`); the Worlds page and `AutoBackup` share it. `backup(entry, Kind = Manual)` takes `Manual` or `Auto`.
  At most one job per world key; a request for a busy world is refused. A job is refused when the instance runs or is being launched, checked on the GUI thread at request time. **Launch lock**: while a
  job runs for an instance, `WorldBackupJobs::isInstanceLocked(id)` is true and `Application::launch` (one `// ZERO` block at its top) refuses with the message box "A world of this instance is being
  backed up or restored. Try again when it finishes." Other services can hold the same counted lock with `lockInstance` / `unlockInstance`. Signals: `jobStarted`, `jobFinished(key, ok, message, automatic)`
  (`automatic` = the job was requested as `Kind::Auto`; the Worlds page toasts only non-automatic results), `backupsChanged`, `worldRestored`, `backupsListed`.
- **Automatic backups** (`launcher/zero/backup/AutoBackup`, no widgets, no `APPLICATION`; created in `Application` right after the jobs object and reachable as `AutoBackup::global()`): after a session ends
  the worlds that were played are backed up as `Kind::Auto`. Setting `ZeroAutoBackupWorlds` (global bool, default true; checkbox "Back up automatically after playing" at the bottom of the Backups block).
  - `onGameRunning(id)` (from `launchPhaseChanged(Running)`) remembers the session start; `onLaunchEnded(id, success, aborted, gameStarted)` does nothing unless `gameStarted` and the setting is on.
    Played, stopped and crashed sessions are all backed up. `launchEnded` fires before the phase returns to Idle, so the work is deferred with `QTimer::singleShot(0)` (the running check passes then).
  - Selection (pure, `selectWorlds` / `touchedSince`): a world is selected when its `level.dat` modification time is >= session start - 5 s (`kAutoSlackMs`). The scan (`scanWorldsDir`, mtime read,
    `folderSize` of touched worlds only) runs on a pool thread and is posted back with the QPointer pattern. Touched worlds over `kAutoMaxBytes` (2 GiB) are not backed up but reported as "too large".
  - The selected worlds are backed up **sequentially** (the next one starts on the previous `jobFinished`). A refused or failed job counts as failed and the queue goes on. **The instance stays locked**
    (`lockInstance`) from `launchEnded` until the queue ended (scan included), so a new launch can never slip in between two queued backups and no backup is refused because of it.
  - When the work of an instance ended it emits `finished(instanceId, instanceName, backedUp, tooLarge (world names), failed, firstError)`; nothing selected = no signal. `MainWindow` shows up to three
    toasts per instance (Success "Backed up N world(s) of X", Error "Automatic backup failed for X: ...", Warning "... is too large for automatic backups ..."), never one per world.
- **Backup destination** (`launcher/zero/backup/BackupDestination`, pure, no widgets, no `APPLICATION`): setting `ZeroBackupRoot` (string; empty = the default root `<data>/zero/backups`). A chosen folder `<F>` stores
  new backups in `<F>/ZERO Backups/<instanceId>/<worldFolder>` (`rootInFolder`, `worldBackupDirIn`). `detectCloudFolders(Environment)` finds the cloud folders offered in Settings through an injectable environment
  (env vars, `dirExists`, `readFile`, `driveRoots`; tests use a fake PC): OneDrive (`%OneDrive%`, else `%OneDriveConsumer%`), Google Drive for desktop (`<drive>:/My Drive` on every ready volume, else
  `%USERPROFILE%/Google Drive/My Drive` or `%USERPROFILE%/My Drive`) and Dropbox (`personal.path`, `business.path` of `%LOCALAPPDATA%` / `%APPDATA%` `/Dropbox/info.json`); only folders that exist.
  - **Never silent**: `WorldBackupJobs` reads the setting at request time (`Destination::chosenFolder`) and probes it on the pool thread (`probeFolder`: the folder must exist, `ZERO Backups` is created and a probe
    file is written and removed; the chosen folder itself is never created). When it fails, that backup (or the safety backup of a restore) goes to the default root, the job's message gets
    "Saved in the ZERO folder: <folder> isn't reachable" appended and `fellBackToDefault(key, folder)` is emitted just before `jobFinished`. `AutoBackup::finished` and `ScheduledBackup::finished` carry the
    folder (`fallbackFrom`) and `MainWindow` shows it in the summary toast. Settings > Launcher > "World backups" shows the resolved path and a warning caption when the current choice is not writable.
  - **Several roots**: the default root is always known; every other root that received a backup or was applied in Settings is remembered in `<data>/zero/backup-roots.json` (`KnownRoots`, `Destination::knownRoots` /
    `rootUsed`; `WorldBackupJobs::rememberRoot`). `refreshList` merges the backups of the world from all known roots (and the active one), newest first. `restore` and `remove` accept a zip only when it lies in
    `<known root>/<instanceId>/<worldFolder>` of that very world (`rootOwning`; anything else is refused with "This backup does not belong to this world.") and `remove` passes the matching root to `deleteBackup`.
    A restore reads from any root, but its safety backup and the retention belong to the destination in use (the default root on fallback); retention only ever touches the folder just written to.
    "Open backups folder" opens `backupDir(entry)`: the active destination's folder, else the first known root's folder that exists.
- **Scheduled backups** (`launcher/zero/backup/ScheduledBackup`, no widgets, no `APPLICATION`; created in `Application` next to `AutoBackup`, `ScheduledBackup::global()`): setting `ZeroBackupSchedule` = `off` (default) /
  `daily` / `weekly`. A catch-up check runs 2 minutes after start-up, then one every hour (`Timing`). A run is due (`isDue`) when the last completed run (`<data>/zero/backup-schedule.json`, `lastRunMs`) is
  older than 24 h / 7 days, never happened, or lies in the future (clock set back). It only starts while no game runs or launches (any instance with `launchPhase != Idle` or `isRunning()`); otherwise the next
  hourly check tries again.
  - Selection (pure, `selectScheduled` / `needsBackup`): a world is backed up when it has no backup in any known root, or its `level.dat` is newer than the creation time of its newest backup (any kind, any
    known root; `newestBackupMs`). Worlds over `kAutoMaxBytes` count as "too large" and are skipped. The scan runs on a pool thread and is posted back with the QPointer pattern.
  - The backups are `Kind::Auto` jobs of `WorldBackupJobs`, one world at a time, across all instances. An instance is locked (`lockInstance`) while its queue is pending. Before an instance starts, the service
    checks again that no game runs: if one does, the run is **interrupted** (not recorded; finished worlds are not backed up twice at the next check). An instance that another service already holds locked
    (the automatic backup of a session that just ended) is skipped. A refused or failed job counts as failed.
  - The completion time is recorded even when nothing needed a backup. `finished(backedUp, failed, tooLarge, firstError, fallbackFrom)` is emitted for every run; `MainWindow` shows one toast built by
    `summaryText(...)` only when something was backed up or failed ("Scheduled backup: 3 worlds backed up, 1 world failed (...). Saved in the ZERO folder: X isn't reachable.").
  - Settings > Launcher > "World backups" (`LauncherPage`): "Back up worlds after playing" (= `ZeroAutoBackupWorlds`, kept in sync with the checkbox of the Backups block through `SettingChanged`), "Scheduled
    backups" (Off / Every day / Every week) and "Save backups to" (default, detected cloud folders, the current custom folder, "Choose folder...").
- Tests: `tests/WorldBackup_test.cpp` (injected rename / free-space / trash hooks simulate failures), `tests/AutoBackup_test.cpp` (selection rules and the service with real temp worlds),
  `tests/BackupDestination_test.cpp` (effective root, cloud detection with a fake PC, probe, fallback, known roots), `tests/BackupRoots_test.cpp` (the job runner with a chosen / unreachable destination and several
  roots: listing, remove, restore, refusal outside the known roots), `tests/ScheduledBackup_test.cpp` (due computation, selection, summary text and the service).
- Known limits: nothing prevents another program from editing a world during the copy (the verify step would then fail the backup rather than record a torn one); a restore sets file modification times
  to "now"; empty folders inside a world are not restored.

### Modpack snapshots (safe update, Mission 003 task 6b)
A modpack update must never cost the player an instance. Code: `launcher/zero/backup/PackSnapshot` (pure core, namespace `Zero::Backup::Pack`, no widgets, no `APPLICATION`, seams in `Pack::Hooks`: trash,
rename, free space, clock) and `PackSnapshotTasks` (`PackSnapshotTask`, `PackRollbackTask`: `Task`s on the pool, shown by Prism's `ProgressDialog`). It reuses the zip engine of `WorldBackup`, extended without
changing the world behavior: `Backup::Options` (exclude predicate for files and folders, fixed zip name, `extra` manifest object, cancel poll, progress), `readBackupInfo`, `extractBackup` (verify, extract into a
staging folder, verify again) and `renamePath`.
- **Why before the task**: Prism's Modrinth / Flame update tasks remove the old pack files inside the instance folder while they run and `InstanceList::commitStagedInstance` then copies the new ones over it
  (`FS::overrideFolder`). `MainWindow::installModpack` therefore takes the copy right after the dialog was accepted with `isUpdate()` and **before** `extractTask()`; if it fails the update does not start
  ("Could not save a copy of X first, so it was not updated: <reason>"), cancelling the progress cancels the update ("Update cancelled. X was not changed."). It is refused while the game runs
  (`Application::launchPhase` / `isRunning`) or another backup job holds the instance (`WorldBackupJobs::isInstanceLocked`; the tasks hold the same launch lock while they run).
- **Snapshot** = `<data>/zero/snapshots/<instance id>/pack-<yyyyMMdd-HHmmss>[-n].zip` (UTC stamp; the folder is shared with the mod snapshots of task 6a, which are folders, not zips) with the manifest of
  `WorldBackup` (`world/<path relative to the instance folder>`, SHA-256 per file, written as `.part`, verified, renamed) and a `.json` beside it (format 1: instance id and name, pack id / name, old and new
  version, game folder, created time, `outcome` pending | ok | failed, `rolledBack`). The zip cannot change after it was written, so the outcome and the roll back mark live in the `.json` (written with
  `QSaveFile`); without it the zip's own manifest `extra` is read and the outcome is pending. Never part of it (`isExcluded`): `<game dir>/saves`, `screenshots`, `logs`, `crash-reports`, `.cache`
  (the game dir comes from `gameRoot()`, never guessed) and anything below a `.zero-*` folder. Links and junctions are not followed (a linked `mods` folder is therefore not in the copy). Free space: size x 1.1
  + 50 MiB on the volume of the snapshots folder.
- **Outcome**: after the update task the copy is **kept** (success or failure) and marked ok / failed. When the provider created a new instance instead of updating this one (`createdId != updateId`) the copy
  is removed (`removeSnapshot`, ZERO's own file). **Retention**: the newest 2 `pack-*.zip` per instance (`kKeep`, rolled back ones count), older ones and their `.json` are removed; a `.part` older than an
  hour too. Only names that match the pattern are touched. Snapshots of deleted instances are left in place (the delete-instance flow is unchanged); a recreated instance with the same id never offers them
  because the pack id must match (`newestActive(list, packId)`, checked again by `rollback`).
- **Roll back** (`Pack::rollback`, order matters): 1. the snapshot must be in this instance's snapshots folder, of this instance id and pack id, not rolled back; free space; 2. `extractBackup` into
  `<instances>/.zero-restore-<id>-<ts>` (the zip is verified before and the files after); 3. `instance.cfg` of the staging folder = the CURRENT file with only the `ManagedPack*` keys of the [General] section
  taken from the snapshot (`mergeInstanceCfg`: play time, ZERO profile keys and Java settings stay; written with `QSaveFile`); 4. the player's folders (`playDataFolders`: saves, screenshots, logs,
  crash-reports of the game dir) are **renamed** from the instance into the staging folder, and so is every symlink / junction / shortcut found in the instance (walk without following links, `findLinks`; the zip
  engine skips them, so a linked `mods` folder is not in the snapshot) when its place in the staging folder is free (a link whose place the snapshot holds stays with the old instance); 5. rename instance -> `.zero-replaced-<id>-<ts>`, rename staging -> instance; 6. mark the snapshot rolled back;
  7. the replaced folder goes to the Recycle Bin (`FS::trash`, **no permanent-delete fallback**). Any failure up to step 5 puts everything back (carried folders move back, the staging folder is removed only
  when nothing of the player's is in it) and says "Your instance was not changed"; if even the way back fails the message names the folders that hold the instance and the play data. A trash failure after
  the swap is not a failure: the instance is the old one, the replaced folder stays where it is (renamed `.zero-leftover-<id>-<ts>` so that recovery cannot take it for a cut-off roll back and bring a deleted
  instance back) and the toast says where. Everything else the player did after the update outside those folders (options.txt,
  servers.dat, mods added since) is the snapshot's again, and what was replaced is in the Recycle Bin.
- **Startup recovery** (`Pack::recoverInterrupted(instancesDir)`, run by the `InstanceList` constructor for every instances folder, before the first discovery; one `// ZERO` block): a crash or power
  loss during a roll back would leave the worlds in a hidden `.zero-restore-<id>-<ts>` folder or the instance only as `.zero-replaced-<id>-<ts>`. Per id: replaced exists and the instance does not -> renamed
  back; the instance and a staging folder -> every play-data folder and link of the staging folder whose place is free in the instance moves back, and the staging folder (ZERO's extracted copy) is removed
  only when no folder named like play data and no link remains in it (else it is kept and logged); the instance and a replaced folder and no staging folder (crash after the swap) -> the replaced folder
  goes to the Recycle Bin; a `.zero-leftover-*` is offered to the Recycle Bin again when its instance exists. Nothing is overwritten, every action and problem is logged (`ZERO recovery:`).
- **After the swap** `InstanceList::reloadInstance(id)` (one `// ZERO` method) drops the in-memory instance (stale settings, components, mod lists and their folder watchers; a stale `PackProfile` would write the
  new pack's `mmc-pack.json` back) and `loadList()` discovers the folder again; `instanceSelectRequest` selects it. `InstanceList::discoverInstances` skips folders named `.zero-*`: the staging and replaced
  folders hold an `instance.cfg` and would otherwise appear as instances.
- **UI**: success toast "Updated X" with the single action **Undo** (replaces "Show in Library" for updates; installs keep it); Library details panel button **Roll back update** (icon `arrow-left`,
  `InstanceDetailsPanel::refreshRollback`) only while the instance has a not rolled back snapshot of its pack, tooltip "Back to <old version> (saved <relative time>)", disabled while the game runs; the question
  "Roll back X to <old version>?" / "Your worlds, screenshots and play time stay. The updated files go to the Recycle Bin." (`ModsUi::ChoiceDialog`, Cancel / Roll back, Enter cancels); a modal progress while it
  runs; toast "Rolled back X" or the error. Dev aids: `ZERO_SCREENSHOT_PACK_SNAPSHOT=<instance id>` (+ `ZERO_SCREENSHOT_PACK_NEW_VERSION`) takes a snapshot, `ZERO_SCREENSHOT_PACK_ROLLBACK=<instance id>` grabs the question.
- Tests: `tests/PackSnapshot_test.cpp` (exclude rules, cfg merge, snapshot contents and meta, retention, listing, happy-path roll back, every failure path with injected rename / trash / free-space hooks,
  corrupt zips, the tasks: failure gates the update, cancel, refusal while running or locked). Known limits: a crash between the carry-over and the swap leaves the player's folders in the hidden `.zero-restore-*`
  folder (named in the log); the copy is as big as the instance without its play data (mods and resource packs can be gigabytes); a task that is cancelled while it verifies finishes that step first.

## Test copies ("Try it in a copy")

Try mods, versions or settings in a throw-away copy of an instance, then bring the changes back or discard the copy. A test copy is an ordinary instance made by Prism's
`InstanceCopyTask` (a full, independent copy: no symlinks / hardlinks / clone, no screenshots, no play time, worlds optional) plus one key in its `instance.cfg`:
`ZeroSandboxOf=<id of the original>` (registered by `BaseInstance`; `InstanceCopyTask::setZeroSandboxOf` writes it inside the task, so the copy is marked from the moment it exists, and a
manual Prism "Copy" of a test copy clears it). Nothing else marks it; the name is `<X> (test copy)` (Prism makes it unique), same group and icon.

- **Service layer** (`launcher/zero/sandbox/`, no widgets): `Sandbox` (the key, `testCopyName`, `copyPrefs`), `SandboxPlan` (pure, read-only: `comparePlan` says per part whether the copy differs from
  the original; parts = Mods `mods/` incl. `.index`, Config `config/`, Resource packs, Shader packs, Game version and loader = `mmc-pack.json` + `patches/`, Worlds = the copy's `saves/<name>`
  folders with a `level.dat` that are new or changed; a missing folder equals an empty one; files compare by size, then time within 2 s, then the bytes (up to 64 MB); `.zero-*` and `session.lock` ignored;
  links are never followed), `SandboxApply` (the file moves, see below) and `SandboxTasks` (`CompareTask`, `BringBackTask`: `Task`s on the pool for Prism's `ProgressDialog`; the bring-back task locks both
  instances like the backup jobs and refuses while either is busy).
- **Bring changes back** (`MainWindow::bringChangesBack`): refuse (toast) while the copy or the original is busy (`packBusyReason`, re-checked after every dialog) → `CompareTask` → the dialog
  (`SandboxUi::bringBackPrompt`: one checkbox per differing part, all on except Worlds; "Discard the test copy afterwards" on; "A copy of X is saved first, so you can undo this."; nothing differs =
  "No changes to bring back" with Close / Discard copy) → **`PackSnapshotTask` of the original** (the modpack snapshot engine, meta `kind = "bring-back"` and `label = "Before bringing back changes from
  <copy>"`; cancellable; a failure stops with nothing touched) → `BringBackTask` → `reloadInstance(original)` → optionally `trashInstance(copy)` → toast "Brought changes back to X" with **Undo**
  (= `rollBackModpackUpdate(original)`: the same roll back of the snapshot; its question and the Library row "Undo bringing changes back" read by `meta.kind`; worlds are never rolled back, their old
  versions are the safety backups: the toast adds "Old worlds are saved as backups in Worlds."). The snapshot is removed when the bring-back failed and everything was restored; `newestActive` takes the
  newest not rolled back snapshot of any kind (an empty pack id fits any), and the details panel shows the roll back row for a non-modpack instance only for a "bring-back" snapshot.
- **`SandboxApply::apply`** (data safety): targets = the differing files / folders of the chosen parts (worlds: only changed or new ones). (1) Every world about to be replaced gets a Safety world backup
  (`safetyBackupHook`, into the destination-aware backup folder of that world; a failure stops everything). (2) The copy's version of each target is copied into
  `<original>/.zero-bringback-<ts>/new/<n>` (the original is untouched; a failure removes only that ZERO-owned folder). (3) The swap, per target two renames: the original to `.../old/<name>`, the new one
  into place; any failure puts every target back in reverse order (`Result::restored`; if even that fails the staging folder is kept and named in the error). (4) The replaced originals go to the Recycle
  Bin (`Hooks::trashPath`, default `FS::trash`, never a permanent delete; one that cannot go stays in the staging folder and is listed in `Result::leftovers`). Only ZERO's own `new/` copies and empty
  staging folders are ever deleted. Worlds only in the original stay; worlds only in the copy are added. The window between the two renames of one target is two renames long; a crash inside it leaves the
  originals in `.zero-bringback-*/old` (and the safety copy of the instance exists).
- **UI** (`launcher/ui/zero/sandbox/`): `SandboxDialogs` (`CheckDialog`: the frame of `ModsUi::ChoiceDialog` with checkboxes + captions; `startPrompt`, `bringBackPrompt`, `chosenParts`) and `SandboxBanner`
  (tinted `info` card: "Test copy of X" + Bring changes back / Discard copy, or "The original instance is gone" + Keep as a normal instance / Discard copy; `Look::Card` in the Library details panel,
  `Look::Compact` under the instance window header card; buttons disabled with the reason while busy). The clicks travel through `SandboxUi::Requests` (one object, so the instance window needs no
  pointer to the main window); `MainWindow` connects `tryInCopy`, `bringChangesBack`, `discardTestCopy`, `keepTestCopy`. "Try it in a copy" is a Library details button after Copy (icon `git-branch`,
  hidden on a test copy, disabled with the reason while the instance is busy) and an entry after Copy in the instance context menu. Discard = confirm ("It goes to the Recycle Bin.") →
  `trashInstance`; never a permanent delete.
- Tests: `tests/SandboxPlan_test.cpp` (folder comparison, parts, worlds, `copyTree` keeps times), `tests/SandboxApply_test.cpp` (replace / add / remove parts, Recycle Bin hook, injected copy / rename failures
  restore everything, failed restore keeps the staging folder, trash failure, worlds replaced / added / kept, backups before the first change, the tasks), `tests/SandboxMeta_test.cpp` (`ZeroSandboxOf` round
  trip, copy prefs, snapshot kind + label, `newestActive` with an empty pack id, undoing a bring-back), `tests/ZeroSandboxDialogs_test.cpp` (the dialogs and the banner; `ZERO_SANDBOX_SHOTS=<dir>` writes PNGs).
  Not covered by tests: the `MainWindow` orchestration (it needs the whole application).

## Storage Manager (Mission 002)
Core only (no widgets, no `APPLICATION`, no Prism instance classes): `launcher/zero/storage/StorageScanner` (namespace `Zero::Storage`). The UI fills plain `InstanceSource` / `LauncherSource` structs from the
instance list (like `sourcesFromApplication()` in `WorldsPage`). Rule: measure everything, never delete without clear consent, never auto-delete user content.
- **Categories** (`Category`, `categoryId()`): per instance Worlds (`worldsDir`), Mods (`modsDir`), Packs (all `packDirs`), Screenshots (`gameRoot/screenshots`), Logs (`gameRoot/logs` + `gameRoot/crash-reports`),
  OtherInstance (the rest of `instanceRoot`); launcher Backups (`<dataRoot>/zero/backups`), Libraries (`libraries` + `assets`), Java (`java`), Cache (`cache` + `meta` + the `metacache` file), OtherLauncher (everything
  else under `dataRoot`: rest of `zero/`, `logs/`, icons, themes, config files; the instances dir is excluded, it is reported per instance). Paths are compared normalized and case-insensitively.
- **Every byte once**: the same path configured twice counts once (first category wins); a configured dir nested in another is subtracted from its parent; missing dirs are 0. The "other" buckets are measured by walking
  the root and skipping the configured dirs (`sizeExcluding`, built on `Worlds::folderSize`, so symlinks and junctions are never followed or counted), not by `total - parts`, so the big trees are walked once and a link
  can never make a bucket negative. Stray files directly in the instances dir (e.g. `instgroups.json`) belong to no category.
- **`buildReport(instances, launcher, nowMs)`** does all disk work synchronously (pool thread): `StorageReport` = totals per category, per-instance rows (`InstanceRow`, biggest first), cleanup groups (always both kinds),
  restore leftovers, free / total bytes of the data root volume (`QStorageInfo`, -1 when unknown).
- **Cleanup candidates** (the only things ever offered): regular files, links / junctions / shortcuts excluded, directly (non-recursive) in `gameRoot/logs` (`*.log`, `*.log.gz`, never `latest.log` / `debug.log`) or
  `gameRoot/crash-reports` (`*.txt`), last modified more than `kCleanupAgeDays` (30) before `nowMs`. A `logs` / `crash-reports` folder that is itself a link is not scanned. A running instance (`running`, set by the
  provider) yields nothing. Worlds, mods, screenshots, packs and backups are measured only. `.zero-restore-*` / `.zero-replaced-*` folders in a worlds dir are reported as `RestoreLeftover` (info only, never deletable
  through ZERO; they can be the only copy of a world). A leftover seen while a restore job runs is that job's staging folder: the UI can use `WorldBackupJobs::isInstanceLocked`.
- **`trashCandidates`** is the only writer. For each file it re-validates NOW with the same predicates as detection (`isCleanupCandidate`, `cleanupDir`, `matchesCleanupName`, `isOlderThanCleanupAge`): regular non-link
  file, parent is exactly the logs / crash-reports folder of a known instance (path tricks like `logs/../mods` fail), no owner of that folder is running, name still matches the kind, still older than 30 days. Refusals are
  counted as skipped with a reason. Valid files go through `TrashHooks::trashPath` (default `FS::trash` = Recycle Bin). **A failed move leaves the file in place; there is no permanent-delete fallback and no delete hook.**
- **`StorageScanner`** (QObject): provider (instances + launcher) called on the GUI thread by `rescan()` and by `trash()` (fresh `running` flags); one pool job per scan; generations drop older results; jobs only hold a
  `QPointer` and post to the application object (same pattern as `WorldScanner`). Signals `scanStarted`, `reportReady(StorageReport)`, `trashFinished(TrashResult)`; one trash job at a time, a second request gets a
  `trashFinished` whose `error` is set. The UI rescans after a trash.
- Tests: `tests/StorageScanner_test.cpp` (temp trees, junction cases, injected trash hook).

## Java Doctor (Mission 002)
Core without widgets / `APPLICATION` / Prism instance classes: `launcher/zero/java/JavaDoctor` (namespace `Zero::Java`). Rule (MISSION): never change a Java selection without permission; the core never writes anything.
- **Inputs** are plain structs: `Runtime` (path, version, major, 64-bit, managed), `GlobalJava` (`AutomaticJavaSwitch`, `AutomaticJavaDownload`, pinned paths per major + the ones that do not resolve, download support), `InstanceJava` (effective settings of one
  instance + what its version needs: compatible majors, Mojang runtime name, the downloaded runtime path when it exists).
- **`diagnose(instance, runtimes, global)`** is pure and predicts the launch in Prism's order: `AutoInstallJava` (skipped when automatic switching is off or the instance has its own Java path that exists; else pinned path of a
  compatible major in the order of the majors, then with downloads off the first detected runtime of a compatible major, with downloads on the managed runtime / "will be downloaded"; falling through = use the configured Java),
  `CheckJava` (the configured Java must resolve) and `VerifyJavaInstall` (32-bit with more than 2048 MB warns; a major the version does not support fails unless `IgnoreJavaCompatibility`, then it warns; empty majors = unknown, no verdict).
  `Diagnosis` = status (Ready / Warning / WillFail / Unknown), source (Configured / AutomaticPinned / AutomaticDetected / AutomaticDownload), reason code, effective runtime, and a recommendation (only for Warning / WillFail:
  a compatible installed runtime other than the one in use, 64-bit first, then the highest version; only 64-bit ones for the 32-bit warning). An unknown requirement with no configured Java gives Unknown, not WillFail: the
  requirement is read at launch, where automatic Java may still find a Java.
- **`readRequirements(instanceRoot, metaDir)`**: read-only look at `mmc-pack.json` and the version files (custom `patches/<uid>.json`, else the local metadata cache `meta/<uid>/<version>.json`) for the compatible majors and the runtime
  name. Used instead of loading the `PackProfile` of every instance (loading resolves metadata and may rewrite files); for instances whose component list is already loaded (selected / launched) the loaded launch profile wins.
- **UI gathering** (`JavaDoctorView`): runtimes from `APPLICATION->javalist()` after its load task (PATH entries resolved to the real file, duplicates dropped), globals from the settings, one `InstanceJava` per instance (effective values
  through `settings()->get`). Applying a suggestion writes exactly what the instance Java settings page saves for a manual path (`OverrideJavaLocation`, `JavaPath`, `AutomaticJava` = false); the stored version / architecture /
  signature stay untouched (the signature contains the path, so the next launch re-checks the Java: `CheckJava`).
- `PillTag` (`ui/zero/PillTag`) is the shared bordered label (backup rows, Java Doctor status): plain look or a status tone (`Tokens::statusTint` of success / warning / danger / info).
- Tests: `tests/JavaDoctor_test.cpp` (data-driven table over every branch, recommendation ordering, requirement reader).

## Mod Intelligence (Mission 002)
Core without widgets: `launcher/zero/mods/` (namespace `Zero::Mods`). MISSION rules: separate definite conflicts from possible ones, confidence levels, no false conflict claims. Read-only everywhere (jars are only read, nested jars are parsed from memory, nothing is extracted or written). Outdated-version checks need online metadata and are out of scope.
- **`Versions`** (pure): `compareVersions` (numeric segments, pre-release tags below the release, `+build` ignored; `nullopt` for snapshots like `23w13a`, free text, placeholders and for qualifiers whose order differs between semver and Maven, e.g. `1.20.1-47.1.0` vs `1.20.1`), `matchesFabricPredicate` (`* = > >= < <= ~ ^`, `1.2.x`, space = AND, `||` = OR) and `matchesMavenRange` (`[a,b)`, `(a,]`, `[a]`, unions, bare `a` = at least a). Matchers return `Tri` Yes / No / Unknown; Unknown never becomes a finding stronger than Possible.
- **`ModMetadata`**: own parser (Prism's `LocalModParseTask` drops ranges, breaks, provides and nested jars). One `ModMetadata` per jar = `mods[]` (`ModEntry` per mod and per format: Fabric, Quilt, Forge, NeoForge, LegacyForge/mcmod.info) with `depends` / `recommends` / `breaks` / `conflicts` / `provides`, `nested[]` (Fabric / Quilt `jars`, Forge / NeoForge jarjar; depth <= 3, size and count budgets), `parseError`, `unparsedFormats`. `parseJar(path)` (via `ArchiveReader`), `parseJarBytes` (libarchive on memory) and one parser per format on raw contents. NeoForge `type` maps to depends / recommends / breaks (incompatible) / conflicts (discouraged); `${file.jarVersion}` is read from the manifest, other placeholders mean "version unknown".
- **`ModHealth`**: `InstanceContext` (mc version, loader, loader version; `readInstanceContext` reads `mmc-pack.json`) and the pure `analyze(enabledJars, context, disabledFileNames, disabledJars = {})` -> `Report` (findings sorted Confirmed, Likely, Possible, Unknown, then by mod name; counts; `modsScanned`). Text (title / detail / suggestion) is plain English via `QCoreApplication::translate("ModHealth", ...)`.
- **`ModHealthScanner`**: `analyze(instanceId, modsDir, instanceRoot)` on `QThreadPool::globalInstance()` with a generation counter (same posting pattern as `StorageScanner`), signal `reportReady(instanceId, Report)`. Only `*.jar` (enabled) and `*.disabled` (disabled) files directly inside the mods folder; symlinks skipped. `scanModsFolder` is the synchronous core.
- **Loaded mods**: for the instance loader the first format present in a jar wins (Fabric: Fabric; Quilt: Quilt, Fabric; Forge: Forge, LegacyForge; NeoForge: NeoForge). Environment ids `minecraft java fabricloader quilt_loader forge neoforge fml javafml lowcodefml` are always provided (`fabric` only by a jar that provides it); loader versions are known only for the instance's own loader.

| Finding | Confidence |
|---|---|
| Missing required dependency (no enabled jar supplies the id, nested jars and `provides` included) | Confirmed; Possible when some jar could not be read (it may be the provider). Optional / recommended: no finding. A disabled file that matches turns the suggestion into "Enable ..." (exact with parsed metadata, "may" by file name) |
| Required mod present, version outside the range | Confirmed on No; Possible on Unknown |
| `minecraft` requirement not met by the instance version | Confirmed on No; Unknown: no finding |
| Loader version (`fabricloader` / `quilt_loader` / `forge` / `neoforge`) not met | Confirmed on No; Unknown: no finding |
| Wrong loader: Fabric / Quilt-only jar on Forge / NeoForge (Fabric with Connector installed: Possible); Forge / NeoForge / Quilt-only jar on Fabric / Quilt; NeoForge jar on Forge | Confirmed |
| Forge-format jar on NeoForge | Possible for 1.20.1 - 1.20.4 or an unknown version (early NeoForge still read `mods.toml`), Confirmed after that |
| mcmod.info-only jar on Fabric / Quilt / NeoForge | Likely |
| Several formats in one jar: the weakest alternative decides; broken metadata of the instance's own format is Unknown, never "wrong loader" | |
| Declared `breaks` (Fabric / Quilt) or `incompatible` (NeoForge) matched | Confirmed (also against `minecraft`) |
| Declared `conflicts` (Fabric), `discouraged` (NeoForge), Forge / NeoForge optional dependency whose range excludes the installed version | Likely |
| Duplicate id in two enabled top-level jars (a copy nested in another jar is ignored) | Confirmed; Likely for mcmod.info ids |
| Jar with no recognizable metadata / parse error | Unknown ("ZERO can't read this mod's metadata; it may still work") |

Never reported: `java` requirements, dependencies with `side = SERVER`, anything that depends on a version that cannot be compared. A jar that may still load (Connector case) never counts as "missing" for the ids it provides. Findings are deduplicated (A breaks B and B breaks A is one finding; a missing id is one finding listing its dependents). Tests: `tests/ModHealth_test.cpp` (versions, predicates, ranges, parsers, every rule with positive and false-positive guard cases, real zip round trips with nested jars, scanner).

**Repair actions** (`ModRepair`, first slice of the Repair Center: preview before repair, nothing is lost). The analysis stays read-only; the only change ZERO can make is renaming a top-level mod jar to `<file>.jar.disabled` (Prism's own convention) and back. `actionsFor(finding, enabledJars)` is pure and conservative: wrong loader, Minecraft version and loader version findings, Confirmed only, give one `DisableJar` action for the finding's single top-level jar; a Confirmed duplicate gives one action for every copy except the newest, only when all versions compare and the newest is unique (a tie, an uncomparable or missing version, a jar declaring two versions of the id: nothing); every other finding or confidence (missing dependency, version mismatch, breaks / conflicts, Likely / Possible / Unknown) gets no action and keeps its "Fix:" text as guidance. `scanModsFolder` fills `Finding::actions` (a jar that is only bundled inside another jar is never a target); `Report::allActions()` lists them once per file. `disableJar` / `enableJar` re-validate at call time (plain file name without separators, `:` or `..`, directly in the mods folder, regular non-link file, `.jar` / `.disabled` suffix), refuse when the target name already exists (never overwrite) and retry the rename a few times for antivirus locks. `applyAll` runs the disables one by one: a failure stops nothing and rolls nothing back, the renames that worked are listed in `RepairBatchResult::succeeded` and each is reversible with `enableJar`. Nothing is ever deleted. Tests: `tests/ModRepair_test.cpp` (rules per kind and confidence, duplicate tie / uncomparable, round trip, refusals, a batch with a failing file, scanner integration; the symlink case is skipped without symlink privileges).

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

## Custom themes (Theme editor)
User-made ZERO themes are real themes: `<data>/themes/<slug>/zero-theme.json` (+ `background.<ext>` next to it), id `zero-custom-<slug>`.
- **Core (no widgets)**: `zero/theme/CustomTheme` (definition, JSON `format: 1`, derivation, contrast), `zero/theme/ThemeArchive` (zip export / import, Recycle Bin delete). Unparsable, unknown or newer-format files are skipped with a warning, never fatal.
- **Derivation** (`Custom::deriveSpec`, pure): start from the base spec (Obsidian or Minecraft: bevel, pixel roles, radii), `texture = false` (a custom theme never tiles the stone). `accent` hover / pressed = HSL lightness +-0.07 (dark bg: hover lighter, light bg: hover darker); `onAccent` = black or white by contrast; surfaces step from `bg` toward `text` (0.04 / 0.065 / 0.08 / 0.11 / 0.13), border / borderStrong mix `surface` and `text`, `textSecondary` / `textTertiary` = text at 0.70 / 0.50 over bg; status colors from the base; `overrides` last. Corners: `rounded` = Obsidian radii, `square` = Minecraft's notched look on a Minecraft base and radius 0 on an Obsidian one.
- **ThemeSpec additions**: `baseId` (overlay stylesheet / pixel mark follow the base: `styleId()`), `backgroundImage` (absolute path), `backgroundDim`, `cardOpacity`. `backgroundVideo` (absolute path; a custom theme has an image or a video, never both), `videoSound` / `videoVolume` (the video's sound switch, default off, and its 0..1 volume), `appIcon` (absolute path of the theme's own launcher icon), `fontFile` (absolute path of the theme's font file) and `fontFamily` (the family registered from it, resolved when the theme is applied; empty = the default font or a font that failed to load).
- **Background image**: `ZeroShell::paintEvent` draws it scaled to cover (centered crop), then `bg` at alpha `dim`; the scaled pixmap is cached per path + size.
- **Background video** (`kind: video`): `ui/zero/shell/ZeroVideoBackground`, owned by `ZeroShell`. `QMediaPlayer` + `QVideoSink` only (no `QVideoWidget`, no MultimediaWidgets), `QMediaPlayer::Infinite` loop. It is silent by construction: a `QAudioOutput` exists only while the theme's sound is on (`setSound(on, volume)`, applied live: off deletes the output, on creates it for the existing or the next player) and it is muted whenever the video is not playing (`ZeroVideoBackground::audible(sound, playing)` on top of the play rule below), so the sound stops with the video. Each frame is converted once (`QVideoFrame::toImage()`) and `ZeroShell::paintEvent` draws it with the same cover crop (`coverRect`, source rect straight into `drawImage`, no rescale cache) and `bg` at alpha `dim`. Until the first frame, or if the file cannot be played (codec / plugin missing, file gone), the flat background shows and one warning is logged; never a crash or a black window. The player exists only while the theme has a video: a theme without one calls `setSource("")`, which stops and deletes it (no decoder left running); the same path + mtime is a no-op, so an edited file reloads.
  - **Play rule** (pure, `ZeroVideoBackground::shouldPlay(pageId, minimized, gameRunning, motionOff)`): plays only when the page is `home` or `appearance`, the top-level window is not minimized, no game is running and motion is on; otherwise it is paused and the last frame stays as a still. A theme that opens on another page loads, shows the first frame as the still and pauses. Inputs: `ZeroShell::pageChanged`, `QEvent::WindowStateChange` on the top-level window (event filter installed in `showEvent`), `ZeroShell::setGameRunning` (MainWindow connects `Application::updateAllowedChanged`: false = a game started), `Theme::changed` (motion off, theme switch). Play / pause transitions are logged at debug level (`ZeroVideoBackground: play|pause (page ...)`).
  - **Editor**: BACKGROUND is None | Image | Video; the Video row is the file name + Change (`QFileDialog`, mp4 webm mkv mov) with the same Dim / Cards sliders; the live preview plays the picked source file before saving. `ThemeFiles::saveTheme(root, folder, def, backgroundSource, SaveLimits)` takes an image or a video source (kind from the extension, one list per kind: `imageExtensions()` / `videoExtensions()`, `backgroundExtensions()` is their union), copies it as `background.<ext>` through the same temp / backup / rollback path, and removes the previous `background.*` after the JSON is saved (Image <-> Video leaves one file). A video over `kMaxBackgroundBytes` (200 MB, also the import cap) is refused, in the editor with a toast and again in `saveTheme` (`SaveLimits::maxVideoBytes`, a parameter for tests).
  - **Sound** (JSON `background.sound` bool, `background.volume` 0..1; keys written only with a video; old themes without them = no sound): under the Video row of the editor, a **Sound** checkbox (default off) and, only while it is on, a **Volume** slider 0-100 % (default 50). `ZeroShell::syncVideo` hands `ThemeSpec::videoSound` / `videoVolume` to the video on every theme change, so toggling and dragging apply live.
  - **Release / packaging**: the launcher links `Qt6::Multimedia` (top-level `find_package(Qt6 ... Multimedia)`); `windeployqt` must ship `Qt6Multimedia.dll`, the plugin `multimedia/ffmpegmediaplugin.dll` and the ffmpeg DLLs it needs (`avcodec`, `avformat`, `avutil`, `swscale`, `swresample` and their dependencies). Without them a video theme falls back to the flat background (warning in the log).
- **App icon** (editor section APP ICON, after COLORS: Default | Custom; Custom = a 32 px preview, the file name and Change): `icon.<png|svg|ico>` in the theme folder, JSON `"icon": "icon.png"`, <= 2 MB (`kMaxIconBytes`), raster icons at least 32 px on both sides (an .ico counts by its largest image, an .svg is scalable): `Custom::validateIconSource`, used by the picker and again by `saveTheme`. `Zero::logoPathFor(spec)` answers the file (the theme's icon when the file exists, else the base theme's mark) and feeds `appLogoPath()` / `appLogo()` (window and taskbar icon through `Application::logo()` + `ThemeManager`) and the rail header (`RailHeader` draws it instead of either mark); `applySpecLive` also sets the window icon, so the editor previews it live and Cancel / reapply puts the previous one back. Default and the built-in themes are unchanged. The exe and desktop-shortcut icon cannot change (a Windows resource): the tooltip says "Window, taskbar and sidebar. The desktop shortcut keeps the ZERO icon."
- **Font** (editor section FONT, before CORNERS: Default | Custom; Custom = file row + Change + a preview line "The quick brown fox jumps over the lazy dog" drawn in that font): `font.<ttf|otf>` in the theme folder, JSON `"font": "font.ttf"`, <= 20 MB (`kMaxFontBytes`). `ui/zero/ZeroCustomFont` registers it (`QFontDatabase::addApplicationFont`; at most one theme font at a time, a new file or none removes the previous one; the same path + mtime is a no-op) and `ZeroTheme::apply` stores the family in `Theme::current().fontFamily` before the spec becomes current. `Style::font(role)` puts that family first (the default families stay behind it for missing glyphs) for every role **except** the pixel roles of a Minecraft-based theme; `Style::refreshRoleFonts` re-fonts widgets whose font names a theme family that is no longer wanted (or lacks the wanted one). A file Qt cannot load (also: missing) falls back to the default font with one warning per file, never a crash; the editor's picker refuses such a file with a toast.
- **Opaque windows**: card opacity is for cards inside pages only. `obsidian.qss` paints `QMainWindow, QDialog, QColorDialog, QFileDialog, QInputDialog, QMessageBox, QProgressDialog, QWizard` with `@bgSolid@` and `QToolTip` with `@elevatedSolid@` (both `ThemeSpec::opaque(...)`), and the painted Discover filter popup (`FilterPanel`, its own top-level window) uses `surfaceSolid()`. The theme editor's color dialog does NOT re-apply the application style sheet while it is open: a pick shows only in that row's swatch and hex (`previewSwatch`), and the theme is updated once on OK (`colorSelected`); Cancel puts the row back. A pending live preview is flushed before the dialog opens. (Re-applying the sheet under an open dialog restyled it while its layout was fixed: clipped content.)
- **Card opacity** (0.4..1) is alpha on `surface` / `surfaceHover` only. The QSS color helper emits `rgba(...)` for colors with alpha < 255. Popups must not use it: `ThemeSpec::surfaceSolid()` / `surfaceHoverSolid()` and the `@surfaceSolid@` / `@surfaceHoverSolid@` tokens give the opaque color (QMenu, combo popup list, command palette panel, `QPalette::Base`).
- **Readability**: `Custom::readability(spec)` is Readable when text/bg >= 4.5, text/surface >= 4.5 and onAccent/accent >= 3.0; otherwise HardToRead with the failed pairs in `summary`.
- **Saving**: `ThemeFiles::saveTheme(root, folder, def, backgroundSource, limits, AssetSources{icon, font})` copies each picked file through its own temp name (`.zero-bg-new`, `.zero-icon-new`, `.zero-font-new`) and backup, writes the JSON, then drops the backups and the stale `background.*` / `icon.*` / `font.*` (choosing Default removes the icon / font file); any failure puts every file back and a new theme's folder is removed. `duplicateTheme` copies the background, icon and font too.
- **Loading**: `ThemeManager::initializeWidgets` loads a folder with `zero-theme.json` as `ZeroCustomTheme` before the Prism `theme.json` branch. `ThemeManager::loadCustomTheme(folder)` (re)loads one folder (re-applies it when active) and `removeCustomTheme(id)` unregisters one (falls back to the ZERO default when active), so the editor needs no restart. `specForThemeId` answers for loaded custom ids, so the Appearance page lists them as cards.
- **Import safety**: only the flat entries `zero-theme.json`, `background.<png|jpg|jpeg|webp|bmp|gif|mp4|webm|mkv|mov>`, `icon.<png|svg|ico>` and `font.<ttf|otf>` (at most one of each); json <= 256 KB, background <= 200 MB, icon <= 2 MB, font <= 20 MB (`ImportLimits`); written to `.zero-import-*` then renamed into a NEW slug folder (a clashing name becomes "Name (2)"); a failed import leaves nothing. Delete = `FS::trash` only.

## Add account dialog

One ZERO dialog (`launcher/ui/zero/accounts/AddAccountDialog`, modal, 440 px wide, height to content) replaces Prism's
"Add Microsoft Account" window and the old Ely / Offline dialogs (`MSALoginDialog` is back to the upstream file and unused by
ZERO; `ElyLoginDialog` is gone; `ChooseOfflineNameDialog` stays upstream but is no longer opened). API: `AddAccountDialog::run(parent)`
(starts at step 1) and `run(parent, AccountType)` (opens straight at step 2 without back arrow or "step" overline; used to sign in
again). The dialog never touches the account list: it returns the account and the caller adds it (and makes it the default when it is
the first). Entry points: Settings > Accounts toolbar "Add account", the account menu "Add account..." (above "Manage accounts"),
the first-run setup page, and `LaunchController::reauthenticateAccount` (step 2 of the account's own kind).

**Step 1, "Choose your account"** (overline "Step 1 of 2"): one raised card per kind (icon tile, title, caption, chevron):
Microsoft (`key-round`, "Official account. Every server."), Ely.by (`gem`, "Free account with skins. Servers that accept Ely.by."),
Offline (`user-x`, "Just a name. No online servers."). Microsoft is shown only when `Application::getMSAClientID()` is not empty;
Ely.by and Offline are always there (`Application::SupportsEly` is always on). Click, Enter or Space choose; Up/Down move the focus (ring in accent2).
**Step 2** slides in from the right (320 ms OutCubic through `Zero::Motion`, snapshot overlay, instant with reduced motion); back
slides out.

**Step 2, Microsoft / Ely.by (code)**: header row (back arrow + provider), overline, title "Sign in with ...", a primary button ("Open
Microsoft sign-in" = the browser flow's link, falling back to the code page; "Open Ely.by" = `verification_uri_complete`; when no browser
opens the link goes to the clipboard, like Prism), the caption "or enter this code at <host/path of the flow's url>", the code panel
(QR code of the link the flow gave, with Prism's `?otc=<code>` added for microsoft.com/link; the code in ZERO Pixel (Minecraft) or the mono
font at 20 px; "Copy code" turns into "Copied" for 1.5 s) and the status row (three pulsing squares + "Waiting for you to confirm"; static with
reduced motion). Microsoft runs both `AuthFlow`s (Login = browser, DeviceCode) at once, Ely.by (code) only DeviceCode. A flow's status is the
user's progress only after that flow showed its link/code; the first such status means the user confirmed, and the other flow is aborted.
Success: check + "Signed in as <profile name>", the dialog accepts 900 ms later. A flow that fails before the user confirmed leaves the
other way open; one that fails after (or the last one) turns the status row danger with the flow's first message line, greys the code panel and
shows "Try again" (restarts the flows with a new blank account). A code that expires (`expires_in`) is the same failure ("The code expired.
Try again."). Leaving step 2 (back, Esc, close) aborts the running flows (only running ones: aborting a finished `Task` trips an ASSERT).
**Ely.by password page**: with a client ID the Ely.by step 2 shows the code step first, with the link "Use email and password
instead" (the password page has "Use a code instead" back); without one the password page is the only step. Fields: Email or
username, Password (eye toggle), a slide-open "Two-factor code" (only after Ely answers that it is needed), "Sign in"; footer "ZERO
sends your password only to Ely.by and never saves it." plus links Create an Ely.by account / Forgot password?. Re-login of a password
account opens this page prefilled with the saved username (see "Ely.by accounts").
**Offline**: "Play offline", a username field with Prism's rules (3 to 16 English letters, numbers or underscores; "Allow invalid
usernames" keeps the warning and allows any non-empty name), "Add account" disabled until valid. Prism's rule that an offline account needs
another account that owns Minecraft stays: without one the field is disabled and the hint says so.
Test seams: `AddAccountEnvironment` (kinds offered, the `AuthFlow` factory, open-URL, clipboard, offline factory, success delay); tests
use scripted flows built on a dummy offline `AccountData` (the real steps need `APPLICATION`): `tests/ZeroAddAccount_test.cpp`.
Dev aid `ZERO_SCREENSHOT_ADD_ACCOUNT=<state>` (docs/zero/SHELL.md).

## Ely.by accounts

Third account type next to Microsoft and Offline (`AccountType::Ely`, saved as `"type": "Ely"`). Ported from ElyPrismLauncher
(GPL-3.0) except its `ElyPatchTask`: ZERO runs the game through **authlib-injector** directly (see below).

Two sign-in modes: **email/username + password** (authserver, needs no registration, always available) and **code sign-in** (OAuth
device code, only when a client ID is set).

**Configuration.** `Launcher_ELY_CLIENT_ID` (default `zerocraft-launcher`, ZERO's Website-type app, no secret; `scripts/zero/package.sh`
unsets it for packages built from other IDs) and optional `Launcher_ELY_CLIENT_SECRET` in the root `CMakeLists.txt`
-> `BuildConfig.ELY_CLIENT_ID/_SECRET` -> `Zero::Auth::Ely::configFromBuild()`. Capabilities: `Application::SupportsEly` always on,
`Application::SupportsElyOAuth` when the ID is not empty (empty ID = the code step is skipped and the password page opens directly).
The secret is sent only when set and is never logged. See `docs/zero/CREDENTIALS.md`.

**Password mode** (`launcher/zero/auth/ElyAuthserver.{h,cpp}`: request bodies + answer/error parsing, no network;
`ElyAuthserverNet.{h,cpp}`: Prism `Net` transport). Talks to `https://authserver.ely.by`: `/auth/authenticate` (login),
`/auth/refresh`, and `/auth/invalidate` (best effort when the account is removed). Steps: `ElyPasswordStep` (login, ends in the
profile from the answer) and `ElyAuthserverRefreshStep` (refresh; Ely refuses = `STATE_FAILED_HARD` "Your Ely.by session ended. Sign
in again.", no network = `STATE_OFFLINE`). 2FA: the first try sends the plain password; if Ely answers 401 "two-factor" the dialog
shows the "Two-factor code" field and retries with `password:code`. `accounts.json`: `"type": "Ely"`, `"ely-auth": "password"`,
`"ely-username"`, `"ely-client-token"` (random UUID per account), the authserver access token in the `ygg` slot and the profile.
**The password is never stored**: it is wiped from memory right after the request body is built. A missing `ely-auth` = an old OAuth
account. A failed refresh makes the account re-login (`reauthenticateAccount`), which opens the password page prefilled.

**Code sign-in (OAuth; device code only, Ely.by documents no redirect flow).** Step 2 of the Add account dialog runs only the device code flow
for Ely.by: the primary button "Open Ely.by" opens `verification_uri_complete` (code prefilled) and the QR code encodes that URL.
`AuthFlow` for an Ely account: Login and DeviceCode =
`ElyDeviceCodeStep` (a `MSADeviceCodeStep` with Ely's client, scopes `account_info offline_access minecraft_server_session` and
endpoints `https://account.ely.by/api/oauth2/v1/{devicecode,token}`) -> `ElyYggdrasilTokenStep` (the OAuth access token is the game
session token) -> `MinecraftProfileStepEly` (`GET .../api/mojang/services/minecraft/profile`, then the account counts as owning
the game). Refresh = `ElyRefreshStep` (grant_type=refresh_token; an answer without `refresh_token` keeps the old one; Ely says no =
Expired, no answer = Offline) -> the same two steps. No skin step: skins live on ely.by, so "Manage skins" is for Microsoft
accounts only. The OAuth token is stored in the `msa` slot of `accounts.json` (`msa-client-id` + `msa`), the session token in `ygg`.
List text "Ely.by", top-bar chip subtitle "Ely.by account". Account files without Ely accounts load as before; a type this build
does not know is skipped, never a crash.

**Launch with authlib-injector** (`launcher/zero/auth/AuthlibInjector`). Only sessions of Ely accounts in Normal launch mode
(`AuthSession::wantsAuthlibInjector`) get the extra launch step `EnsureAuthlibInjector`, before the game starts: it fetches
`https://authlib-injector.yushi.moe/artifact/latest.json` (`version`, `download_url` https only, `checksums.sha256`), downloads the jar
to `<data>/zero/authlib-injector/authlib-injector-<version>.jar.part`, checks the SHA-256, renames it and records the hash in
`<jar>.sha256`; a valid jar of that version is reused. If the fetch fails but a jar whose recorded hash still matches exists, the
newest such jar is used (warning in the launch log); otherwise the launch fails with "Ely.by accounts need authlib-injector, which
couldn't be downloaded: <reason>". `LauncherPartLaunch` then adds `-javaagent:<jar>=ely.by` after `MinecraftInstance::javaArguments()`
and before the main class. Microsoft and offline launches never get the step or the argument.

Tests: `tests/ZeroElyAuth_test.cpp` (fake network + `ZERO_LIVE_NETWORK_TESTS=1` live download), `tests/ZeroAddAccount_test.cpp` (dialog,
password page). A real Ely.by sign-in has not been exercised.

## Before you play
When the user presses Play on a Minecraft instance (and a window exists), `Application::launch` calls `Zero::PreLaunchUi::checkBeforeLaunch` (`launcher/ui/zero/prelaunch/PreLaunchDialog`). This replaces the old resource-pack question; the pack rollback (`PackRollbackPrompt`) is now one row of the dialog. The pure core is `launcher/zero/prelaunch/PreLaunchCheck` (+ `zero/security`); the dialog is only its view.
- **Flow.** Setting `ZeroPreLaunchCheck` (global, default on, LauncherPage "Before you play") off: Play at once. Otherwise `runChecks(env, {cachedOnly = true})` runs synchronously (about 1 ms, no jar is opened). `complete` and no items: the launch goes on, no dialog, no delay (the normal path). `complete` with items: the dialog opens on the results. Not `complete` (a jar or the mod metadata is not in the cache yet; the cached-only run then returns no items): the dialog opens in the "checking" state (spinner, "Checking N mods for known malware...", "12 of 140"), the full `runChecks` runs on the global thread pool (`QtConcurrent::run` with a `QPromise` for the progress; the worker only owns copies, so a closed dialog just ignores the late answer) and the dialog then closes itself (nothing found) or shows the results. Scans of one instance are serialized by a per-instance mutex (a scan whose dialog was closed cannot be cancelled and finishes first).
- **Results.** MUST FIX (Block: malware, a fractureiser trace on the PC) and WORTH A LOOK (Warn: duplicate or wrong mods, missing dependencies, packs the game turned off). Each row has its fix buttons; a fix goes through `Session::applyFix` (Recycle Bin via `FS::trash`, `.disabled` rename via ModRepair, Explorer, browser, pack restore) and the rows are rebuilt. "Open Mods" cancels the launch and opens the instance window on the Mods page. Play stays secondary ("Play anyway") while a Block row is open and then asks a second question; Cancel / Esc stores nothing.
- **Cache and settings.** Per-jar results live in `<instance>/.zero/scan-cache.json` (keyed by file name, size and mtime, plus a fingerprint of the mods folder and the instance components for the Mods checks), so only new or changed mods cost time. `ZeroPreLaunchCheck` (global) switches everything off. `ZeroSkipPreLaunchCheck` (per instance, set by "Don't check this instance again" when the user plays) skips the Mods and Packs checks; the malware check always runs. LauncherPage lists how many instances skip and offers "Check them again".
- **Limits.** Only jars directly in `mods/` are scanned (not subfolders, not the other folders of the game directory); the malware look matches known public indicators of compromise only (a new or modified sample is not recognised); the PC traces are the documented fractureiser ones. It is a safety net, not an antivirus.
- **Tests.** `tests/PreLaunchCheck_test.cpp` (core), `tests/ZeroPreLaunchDialog_test.cpp` (dialog and flow with fake scans and hooks; `env.system` is empty, no real PC data; set `ZERO_PRELAUNCH_SHOTS=<folder>` to write PNG grabs).

## Updates for everything
One place on Home that finds the mod updates of ALL instances and applies them through the Downloads center. Engine: `Zero::Updates::UpdateSweep` (`launcher/zero/updates/`, `APPLICATION->zeroUpdates()`, no widgets); real hooks: `launcher/ui/zero/updates/UpdateSweepSetup`; view: `HomeUpdatesSection` (SHELL.md "Home page", 2c).
- **Hooks (all injectable, so the tests use fakes).** `instances()` (id, name, running, busy = a Downloads job of the instance, managed pack), `checkFactory(instanceId)` (a `ModsUi::ModUpdateCheck`, the same seam as the Mods page; nullptr = nothing to check), `apply(instanceId, updates, started, done)`, `autoEnabled()`, `now()`. The real check (`InstanceModCheck`) loads the instance's `loaderModList()` (`startWatching()` or `update()`, waits for `updateFinished`, stops watching again if it started), selects the mods the Mods page would check (`canCheck`: metadata, usable project id, not "locked"/pinned, CurseForge only with a key) and runs Prism's checkers through `prismUpdateCheckFactory`. Mods only: shader packs and resource packs update through the modal `ResourceUpdateDialog`, there is no headless checker for them (`UpdateEntry::kind` and `captionFor` already know the other kinds).
- **Checks.** One queue, one check at a time, a 60 s timeout each (the check is aborted, the instance gets an error record, the next one is checked). Skipped: running instances (also during Preparing), instances with a running download, managed modpack instances (`isManagedPack()`: their mods are updated through the pack), instances the factory has nothing for. `checkNow()` ignores the 24 h rule and does nothing while a check of all instances runs. Signals: `started`, `progress(done, total, currentName)`, `finished`, `resultsChanged`, `stateChanged(id)`, `problem(id, message)`.
- **Summary file** `<data>/zero/updates.json` (`QSaveFile`, `{version, checkedAt, instances: [{id, name, checkedAt, error, entries: [{key, name, version, kind}]}]}`): written after every change, read at start (an unreadable file is moved to `.corrupt`), instances that vanished are dropped (on load, after a check, and `results()` never lists them). Only instances with updates or an error are kept; a failed instance is not listed.
- **Automatic check.** `start()` arms a timer (30 s); each look: setting `ZeroAutoCheckUpdates` (default on) off -> look again in 1 h; last check younger than 24 h -> look again later; a game runs -> look again in 60 s (and 5 s after `launchPhaseChanged` / `launchEnded`, `onGameStateChanged()`); else `checkNow()`.
- **Updating.** `updateInstance(id)` queues an Update work item (before the pending checks of a running sweep): a FRESH check of that instance (tasks of an older check are never reused), then the applier. `updateAll()` does that for every instance with updates, in turn: the next instance's check starts once the previous job STARTED (downloads may overlap, checks never). Real applier: snapshot of the files being replaced on a pool thread (the same `Mods::Snapshot` as the Mods page, so the page's Undo notice works), then `applyUpdatesInBackground` (a Downloads job of kind Update "Update N mods" with the instance name), `finalizeSnapshot` when the job ended; the instance is re-checked for "running" right before the job starts. A job that ended Done drops the instance's entries; a failed or cancelled one keeps them. A running / busy instance is not updated (`problem`); a failed fresh check reports `problem` and starts nothing.
- **UI hooks.** Palette "Check for updates" / "Update all" (`MainWindow::buildPaletteCommands`), Settings › Launcher › Updates checkbox, Home row click -> instance window, page "mods".
- **Tests.** `tests/UpdateSweep_test.cpp` (engine), `tests/ZeroHomeUpdatesSection_test.cpp` (view; `ZERO_UPDATES_SHOTS=<folder>` writes PNGs). The real hooks (`UpdateSweepSetup`) need Prism's models and the network and are verified by running the app, not by unit tests.

## Downloads center
Background installs. Instead of running an install `Task` under a modal `ProgressDialog`, a flow hands it to `Zero::Downloads::DownloadCenter` (`launcher/zero/downloads/`, `APPLICATION->zeroDownloads()`, also `DownloadCenter::global()`). The core has no widgets; the UI is the top bar `DownloadsButton` and the `DownloadsPanel` popover (`launcher/ui/zero/shell/`, wired in `MainWindow`). Status: in use. Converted flows (background jobs, no modal dialog): Discover mod / resource pack / shader / world installs (`MainWindow::installMod`, `installFolderContent`, `installWorld`), the combined cart install and Boost performance (`runInstallPlan(target, plan, done)`), the Mods page updates (`ModsUpdates::applyUpdatesInBackground`, kind Update) and the Shader packs page updates. They start through `Zero::Downloads::startFlow` (`ui/zero/shell/DownloadFlow`: the toast "Downloading <title>…" with **Downloads**, and on the end an Error toast "Couldn't download <title>" for Failed / a Warning toast "Downloaded with warnings" for Done with warnings; the history keeps the error text and the warnings). Still modal on purpose: `installModpack` / instance creation (the provider tasks open their own dialogs), `saveCopyBeforeModpackUpdate` and `rollBackModpackUpdate` (short, they swap the instance folder and must not overlap other work), `runModalTask`, the remote-URL downloads of `processURLs` (upstream code) and the drag and drop add (local copies, nothing to download). Guards while a job of an instance runs: Play (`Application::launch`), deleting the instance (`on_actionDeleteInstance_triggered`: "Files for this instance are still downloading. Try again when they finish."), renaming its folder (`askToUpdateInstanceDirName`: only the name changes), a modpack update / roll back (`packBusyReason`). That is what keeps the models inside a `ResourceDownloadTask` valid.

**API** (namespace `Zero::Downloads`, plain data in `DownloadText.h`):
```cpp
struct DownloadJob {
    QString title;         // "Sodium", "Fabric Fun 1.2", "3 mods"
    QString instanceId;    // blocks Play of that instance while the job runs ("" = nothing blocked)
    QString instanceName;  // shown in the captions
    Kind kind;             // Mod, ResourcePack, ShaderPack, World, Modpack, Mixed, Update (icon + page opened from a history row)
    Task::Ptr task;        // NOT started yet
    std::function<void(const DownloadResult&)> onFinished;
};
struct DownloadResult { State state /*Done|Failed|Cancelled*/; QString error; QStringList warnings; qint64 bytes; };

QString DownloadCenter::start(DownloadJob);          // starts the task, returns the id at once ("" = no task)
bool    DownloadCenter::cancel(const QString& id);   // task->abort() when canAbort(); false = unknown id / cannot abort
void    DownloadCenter::cancelAll();
bool    DownloadCenter::isBusy(const QString& instanceId) const;
int     DownloadCenter::activeCount() const;
QList<ActiveDownload> DownloadCenter::activeJobs() const;   // oldest first: id, title, instance, kind, bytesDone/Total, fraction (-1 unknown), speed, status, canCancel
const QList<HistoryEntry>& DownloadCenter::history() const; // newest first, max 100
void    DownloadCenter::clearHistory();
// signals: jobAdded(id), jobChanged(id) (<= ~10/s per job), jobFinished(id), historyChanged(), activeCountChanged(int)
```
`DownloadCenter(historyPath, clock)` is constructible stand-alone (tests inject the path and a `qint64 ms` clock).

**Converting a flow** (the recipe the flows above follow): build the task exactly as today (usually a `ConcurrentTask` of `ResourceDownloadTask`s, or the modpack / world / pack-update task) but do not `start()` it and do not open a `ProgressDialog`:
```cpp
auto task = makeShared<ConcurrentTask>("Install mods");
task->addTask(...);                                    // as before
APPLICATION->zeroDownloads()->start({ name, inst->id(), inst->name(), Kind::Mod, task,
    [=](const DownloadResult& r) { /* what used to run after the dialog */ } });
```
- `onFinished` replaces the code after `dialog.execWithTask(...)`: it runs once, on the GUI thread, after the job left the active list and the history entry exists (so `isBusy()` is already false). Read `r.state` (Done / Failed / Cancelled), show the toast ("Installed 3 mods" / the error), refresh the page models, do what has to follow a successful install (enable the pack, write the manifest...), clean up on Failed / Cancelled (temp folders). It must not assume the main window or the page that started the install still exist: the process stays alive while downloads run, so look instances up by id and null-check windows. `r.warnings` = `task->warnings()`.
- A flow that needs a result object from the task keeps its own shared pointer to the task (captured in `onFinished`); the center holds the `Task::Ptr` until the callback returned.
- `instanceId` is what blocks Play (`Application::launch`: "Downloads in progress"). Use the id of the instance the files go to (also for a new instance created by a modpack install); leave it empty for a download that touches no instance. The center only guards launching: a page should disable its own second install / delete / edit for an instance with `isBusy()`.
- Progress: bytes = the sum of `current/total` over the distinct `TaskStepProgress::uid`s the task emits (a `ConcurrentTask` forwards its sub-tasks' steps; `NetJob` / `Net::Download` report real byte counts). A sum below 1 KB is taken as "no byte sizes" (a task that counts steps): the bar then follows `Task::progress(current, total)`, and with neither it is indeterminate. The bar never runs backwards (new sub-steps raise the total as they appear). Speed = bytes over the last ~3 s.
- Cancelling: `cancel(id)` calls `task->abort()`; the job ends as Cancelled when the task reports `aborted()` (or `failed()` after the user asked to stop it). A task whose `canAbort()` is false shows no cancel button. `failed` and `aborted` both firing is fine: the first one counts and `onFinished` runs once.
- A task that ends synchronously inside `start()` is still listed (jobAdded) and reported.

**History.** Newest first, at most 100 entries, `<data>/zero/downloads.json` (`{version, entries:[{id,title,instanceId,instanceName,kind,state,error,bytes,finishedAt (UTC ISO)}]}`), written atomically (`QSaveFile`) after every change; running jobs are never stored. An unreadable file is moved to `downloads.json.corrupt` and the history starts empty. A Done entry shows its size only when the task reported byte sizes.

**Application guards** (`launcher/Application.cpp`). `launch()` refuses an instance with `isBusy` ("Downloads in progress" / "Files for this instance are still downloading. Try again when they finish."), next to the backup guard. Running downloads count like a running game for `shouldExitNow()`: closing the last window does not end the process while jobs run, and the process exits when the last job finished and no window is left. `zeroConfirmQuitWithDownloads(parent)` asks "Downloads are still running" / "Quitting cancels %n download(s)." (Quit anyway = destructive, aborts every job; Keep downloading = default). It is used by the tray Quit, Ctrl+Q and `MainWindow::closeEvent` when closing would end the process (`zeroClosingMainWindowQuits()`: last window, no game, no tray icon). With a tray icon, other windows or a running game, closing the main window just continues the downloads.

**UI.** `DownloadsButton` (32 px, between the page actions and the divider before the account chip, `ZeroTopBar::attachDownloads`): hidden while there is no job and no history; accent ring = overall fraction (rotating 90 degree arc when unknown, static with reduced motion), count badge from 2 jobs, tooltip "Downloads" / "Downloading 2 items · 45%", one pulse when a job ends while the panel is closed (skipped with reduced motion). `DownloadsPanel`: 380 px popover (max 480 px, scrolls), right-aligned under the button; header + "Clear history", "In progress" rows (kind icon, title, "Fabric Fun · 12.4 MB of 30 MB · 2.1 MB/s", 4 px bar with shimmer when unknown, cancel) and "Recent" rows (state pill Done / Failed / Cancelled, error line for Failed; a click on a Done / Failed row opens the instance window on the page of its kind, a modpack selects it in the Library; not clickable when the instance is gone). Command palette: "Downloads" while the button is visible.

Tests: `tests/DownloadCenter_test.cpp` (core), `tests/ZeroDownloadsPanel_test.cpp` (panel + button; `ZERO_DOWNLOADS_SHOTS=<folder>` writes PNG grabs).

## Change timeline

Per-instance **Timeline** page of the instance window (`ui/zero/instance/TimelinePage`, id `timeline`, right after Version; the "what changed" line under the title is `PageHelp::forPage("timeline").about`). It is a read-only aggregation of histories that already exist; it has no store of its own and no undo mechanism of its own.

Model: `zero/history/ChangeTimeline.{h,cpp}` (no widgets). `build(Sources)` takes data that was read already (`readSources()` reads the files and the two services) and returns `Entry { timeMs, kind, title, caption, tone, undoable, undone, undoRef }` newest first; `groupByDay(entries, now)` gives "Today" / "Yesterday" / "Mon 14 Nov" groups in the time zone of the injected `now`.

| Source | Entries | Undo (the newest active one only) |
|---|---|---|
| `Mods::Snapshot::listSnapshots` | "Updated N mods" (caption: up to 3 names, "+N more"; one mod: "old -> new") | newest snapshot when finished, valid, not rolled back, not dismissed; Undo = `ModsPage::requestRollback(dir)` (its question, Recycle Bin, toasts) |
| `Backup::Pack::listSnapshots` (`pack-*.zip`) | "Updated <pack> to <new>" / "Brought changes back from <copy>" | `Pack::newestActive(list, packId)`; Undo = `MainWindow::rollBackModpackUpdate(id)` through the `TimelineUi::Requests` signal hub |
| `Packs::readHistory` (`<instance>/.zero/pack-order.json`) | "Changed resource pack order" | newest entry; Undo = `ResourcePacksPage::undo(id)` (`Packs::undoLast`) |
| `DownloadCenter::history()` of this instance | "Installed X" / "Install failed: X" / "Install cancelled: X" (a finished `Kind::Update` job is left out: its snapshot is the entry) | none |
| `SessionHistory::sessions()` of this instance | "Played 1 h 20 min" / "Crashed after 5 min" / "Didn't start" | none |

A rolled-back entry gets the caption tag "Undone" and the muted tone; a dismissed mod snapshot is only not undoable. Unfinished (`pending` / empty outcome) and unreadable snapshots are not listed. Boost / Java argument changes have no persisted history, so they are not on the timeline.

The page shows the newest 100 entries and a "Show more" button (+100). It reads on open, when the game starts or stops, and when the session or downloads history changes (coalesced to the next event-loop turn, deferred while hidden). Undo buttons are disabled with the reason as tooltip while the instance is running / launching or has a background download. The Mods and Resource Packs pages refresh themselves after their own flows (`requestRollback`, `undo`), so they show no stale state; the modpack undo reloads the instance. Tests: `ChangeTimeline_test` (model), `ZeroTimelinePage_test` (widgets, PNGs with `ZERO_TIMELINE_SHOTS`).

## Discord Rich Presence
While a game runs, the Discord desktop client shows what is played. Nothing is shown (and no pipe is open) while no game runs. Code: `launcher/zero/discord/DiscordPresence.{h,cpp}` (no widgets, no `Application`), wired in `Application.cpp` next to the other ZERO services; tests: `DiscordPresence_test` (a `QLocalServer` plays Discord).

- **Discord application.** Public Application ID `1572300937387581450` (`Zero::Discord::kClientId`). The art asset key `zero` is used as `large_image` (`large_text` "ZERO Launcher"); if the asset is missing Discord simply shows no image.
- **Transport.** Discord's local IPC: Windows named pipes `discord-ipc-0` .. `discord-ipc-9` through `QLocalSocket` (all asynchronous, the GUI thread never blocks). Frames are little-endian `uint32` opcode + `uint32` length + UTF-8 JSON. Handshake (opcode 0) `{"v":1,"client_id":...}`, then wait for the `READY` dispatch; `SET_ACTIVITY` (opcode 1) with `args.pid` = the launcher's pid and a fresh `nonce`; clearing sends the same command with `"activity": null`, then opcode 2 (close) and the pipe is closed. PING (3) is answered with PONG (4) with the same payload; partial reads are buffered until a frame is complete; a frame over 1 MB is treated as garbage and the pipe is dropped.
- **Connecting.** On the first running game the service tries pipe 0, then 1, ... up to 9 (a failed pipe moves to the next). If none answers it retries every 30 s (`Options::retryMs`, short in tests) while a game runs. If Discord quits, the pipe closes and the same retry applies. One `qInfo` line on connect and one on disconnect, nothing else is logged.
- **What is shown.** `details` = the instance name (setting `ZeroDiscordShowInstance`), else "Playing Minecraft"; `state` = "Minecraft <version>" plus " · <loader>" for Fabric / Quilt / Forge / NeoForge (read with `Zero::Mods::readInstanceContext` from `mmc-pack.json`; no loader, no suffix); `timestamps.start` = epoch seconds when the game reached the Running phase; `assets` as above. With several games the most recently started one is shown; when it ends the previous still-running one takes over (the service keeps the running games oldest first). The same activity is never sent twice.
- **Wiring.** `launchPhaseChanged` (Running) -> `gameStarted(Game)`, `launchEnded` -> `gameEnded(id)`. The settings `ZeroDiscordPresence` (default on) and `ZeroDiscordShowInstance` (default on) are watched live: turning the first off clears the presence and closes the pipe at once, turning it on while a game runs shows it at once.
- **UI.** Settings > Launcher, group "Discord" after "Language": "Show what you're playing on Discord", under it (indented, disabled while the first is off) "Show the instance name", caption "Discord must be open on this computer." Both are saved on click.
- **Not covered by tests.** A real Discord client (the fake only speaks the documented frames).

## Languages
Languages: **English** (`en_US`, the source language and the default) and **Español** (`es`). Owner decisions and the style of the Spanish
text (neutral Spanish, "tú") are in `docs/zero/I18N.md`.

- **Setting.** Prism's own `Language` setting is the store; ZERO understands two values, `en_US` and `es` (`launcher/zero/ZeroLanguage.h`,
  `Zero::Language`). Any other value Prism may have saved (`es_ES`, `de_DE`) is read as the nearest of the two: a Spanish code is
  Spanish, the rest is English. With nothing saved (first start) the launcher follows Windows: Spanish when
  `QLocale::system().language() == QLocale::Spanish`, else English; `Application` saves the decision, so it is kept from then on
  (`Language::initialCode`, covered by `ZeroLanguage_test`). Dates and numbers follow the language (`QLocale::setDefault`, `Language::localeFor`);
  Prism's `UseSystemLocale` setting is not read any more.
- **Switching.** Settings > Launcher, first group "Language": a combo with "English" / "Español" (each name in its own language, never
  translated). Picking another one saves it at once and asks "Restart now?" (`ChoiceDialog`, buttons Later / Restart now). Restart is
  `Application::zeroRestart`: a running game or running downloads stop it first (the same guards as quitting; the language still applies at
  the next start), then `Zero::Restart::startSuccessor` starts the same exe (with `--dir` only when this run had one) with the variable
  `ZERO_RESTART_WAIT_PID` set, and the launcher quits. The new process waits for that process id to end
  (`Zero::Restart::waitForPredecessor`, called in the `Application` constructor before the single-instance lock, at most 20 s) so that it
  does not just hand its start over to the old launcher. There is no live retranslation.
- **Loading** (`TranslationsModel::selectLanguage`, called by `Application` at start and again whenever Prism's files change). English: no
  translator at all. Español, in this order, each one installed on top of the previous one (the translator installed last is asked first):
  Qt's own `qt_es` / `qtbase_es` when present (the Windows package ships none), Prism's translation for the screens ZERO did not touch
  (Prism's index and `mmc_<key>.qm` / `<key>.po` are downloaded and installed exactly as upstream does; the key is chosen by
  `Language::prismKey`: the system locale when Prism has it, Spain's for a Spanish-Spain system, otherwise `es_UY` "Latin American", then
  `es_ES`), and **last** ZERO's own `:/zero/i18n/zero_es.qm`, so ZERO's text wins. If Prism's download fails or the machine is offline,
  ZERO's strings are still translated. The model lists only English and the Spanish keys of Prism's index (Prism's Language page stays hidden).
- **ZERO's catalogue** is `launcher/resources/zero/i18n/zero_es.ts`: every string of `launcher/` (ZERO's contexts and the upstream strings ZERO
  changed; contexts as lupdate writes them, no file locations). The build compiles it with `lrelease -nounfinished` (`qt_add_lrelease` in
  `launcher/CMakeLists.txt`, the static library `Launcher_zero_i18n`) into the app as `:/zero/i18n/zero_es.qm`. An entry that is still
  `type="unfinished"` is not compiled, so an untranslated string shows in English (or in Prism's Spanish), never as an empty label.
  `scripts/zero/i18n-update.sh` refreshes the `.ts` from the sources keeping the translations that still match (see I18N.md "How to update").
- **Contexts must be the ones the code looks up.** At run time `tr()` uses the class name with its namespaces (`Zero::ModsUi::ModsPage`), which is
  what lupdate writes. Rules that keep it so:
  - A class that calls `tr()` needs `Q_OBJECT` (or `Q_DECLARE_TR_FUNCTIONS(<qualified name>)` followed by `public:` when it cannot have it: the macro
    leaves the section private). Without one, `tr()` quietly resolves to the base class's context and the text is never found (lupdate warns
    "lacks Q_OBJECT": `i18n-update.sh` repeats those warnings for ZERO sources).
  - A `.cpp` without a class (the former free `tr()` helpers) writes `QCoreApplication::translate("<Context>", "text")` at every call. A
    file-local `tr()` wrapper is invisible to lupdate. Tables of texts use `QT_TRANSLATE_NOOP("<Context>", "text")` and translate when they are
    shown (`QCoreApplication::translate("<Context>", variable)`).
  - Never translate at static-initialisation time (a namespace-level `static const QString`): the language is set later.
  - Text the code compares with (`error.contains("running")`) must not be a translated string.
- **Not translated on purpose:** log messages, setting keys, file names, JSON keys, the names of the languages in the picker, and the
  diagnostic `error` texts of `StorageScanner` (the UI only shows counts).
