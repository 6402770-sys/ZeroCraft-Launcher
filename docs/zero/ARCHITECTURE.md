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
- **English only for now** (`launcher/zero/ZeroLanguage.h`, `Zero::Language::kEnglishOnly`). ZERO's own strings have no
  translations, so loading Prism's/Qt's translations mixed languages ("Cuentas" next to "Accounts", Spanish dates in
  English sentences). `TranslationsModel::selectLanguage` always picks `en_US` (no translator, `QLocale` default en_US,
  `UseSystemLocale` ignored), the translation index is not downloaded, and the Language settings page, its palette entry
  and the setup wizard's language page are hidden. The saved `Language` setting is left untouched. To add translations
  later: ship ZERO `.qm` files, install them next to Prism's translator, and flip the flag.
- **Accessibility of painted widgets.** `ZeroCard` gets a factory interface (clickable = Button with a "press" action that
  emits `clicked()`, name falls back to its first label); `PaletteResults` exposes one ListItem per row (StaticText for
  headers, "press" runs the command) whose interfaces are invalidated by a generation counter on every `setRows()`.

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
- Tests: `tests/WorldBackup_test.cpp` (injected rename / free-space / trash hooks simulate failures), `tests/AutoBackup_test.cpp` (selection rules and the service with real temp worlds).
- Known limits: nothing prevents another program from editing a world during the copy (the verify step would then fail the backup rather than record a torn one); a restore sets file modification times
  to "now"; empty folders inside a world are not restored.

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
