# ZERO Launcher — Handoff

Last updated: 2026-11-11 (session 5, Mission 002) · Branch `zero-development` (local only, never pushed) ·
Base: Prism Launcher `develop` @ 00f216ce5 · Repo folder: `C:\Users\64027\Documents\GitHub\ZeroCraft Launcher`
(renamed from `ZERO-Launcher`; the path now contains a space).

## Read first
`docs/zero/MISSION.md` (owner brief incl. Mission 002: binding) → `CLAUDE.md` → `docs/zero/ARCHITECTURE.md` →
`docs/zero/DESIGN.md` (read the **Design v2** section at the end) → `docs/zero/SHELL.md` → `filemap.md`.

## How the owner works
Speaks Spanish, so reply in Spanish. Runs `/savvy-flow` (Opus orchestrates; `savvy-medium` default, `savvy-careful` for
delicate UI refactors/security/migration; one worker at a time on a Claude Pro budget). Local commits on
`zero-development` are authorized as checkpoints; **never push**. Autonomous decisions are expected; don't ask for
ordinary engineering choices. Multi-session roadmap: leave every session compiling, tested, committed, documented.

## Session log
- **Session 1**: toolchain, identity, Prism import, Obsidian design system, shell (rail/Home/Library), Crash Detective.
- **Session 2 (Mission 002, Phase A + start of B)**: theme engine v2 + Minecraft Theme, motion system, Play button with
  real launch states, Appearance page, shell v2 (top bar), command palette. Commits:
  `fca565d87` themes · `59e8b94b5` motion · `7316de327` Play + Appearance · `b0b6aa183` shell v2 · `ffde92420` palette
  (+ this handoff commit).
- **Session 3 (Mission 002, Phase B milestone 6)**: Instance Profiles + Library 2.0: favorites, tags, description
  (= Prism notes), per-instance artwork, filter chips, "Most played" sort, favorite star on cards, artwork covers, details
  panel editing, Home favorites strip + artwork hero, palette favorites. Design/data notes: `docs/zero/SHELL.md` (Library,
  Home) and `docs/zero/ARCHITECTURE.md` (Instance profiles).
  Then **Session History** (`launcher/zero/history/SessionHistory`, wired in `Application`; data
  `<data>/zero/session-history.json`) and the Home **Recent activity** section (6 rows, Diagnose on crashed/failed).
  Then the **Worlds Hub** page (rail item after Library: Alt+3 Worlds, Alt+4 Screenshots, Alt+5 Servers, Alt+6 Tools, Alt+7 Appearance), read-only.
  Then **Smart Backup** (manual world backups + verified restore with a safety snapshot, in the Worlds details panel).
- **Session 4 (Mission 002, Phase C)**: owner asleep, autonomous run following this file's priority order.
  `acaf9c883` **automatic world backups** after playing (`launcher/zero/backup/AutoBackup`; job runner now app-level).
  `5b89db256` + `fdcd3fdd0` **Storage Manager** (`launcher/zero/storage/StorageScanner`, `ui/zero/pages/StorageView`, Tools › Storage).
  `fc358bbf4` **Java Doctor** (`launcher/zero/java/JavaDoctor`, `ui/zero/pages/JavaDoctorView`, Tools › Java Doctor;
  shared `ui/zero/PillTag`).
  `6596ab825` + `c22556aaa` **Mod Intelligence** (`launcher/zero/mods/`, `ui/zero/pages/ModHealthView`, Tools › Mod
  Health, Library "Check mods").
  `742869210` **Repair actions** in Mod Health (first Repair Center slice: `launcher/zero/mods/ModRepair`).
  `bb4950481` **Screenshot Gallery** (rail item after Worlds: Alt+4 Screenshots).
  **Server Vault** (rail item after Screenshots: Alt+5 Servers, Alt+6 Tools, Alt+7 Appearance): read-only `servers.dat` of every
  instance grouped by address, status ping (`launcher/zero/servers/`), details panel, Play on server (quick join).
  Downloads/Update center was skipped for now: it hooks into Prism's network/task engine (higher regression risk on a
  core path); Storage Manager and Java Doctor are self-contained and came first.
- **Session 5 (owner present)**: real-app test of the dev build against a temp data dir (rail navigation, Back up now,
  Restore incl. confirm dialog + safety row + Recycle Bin, palette query, theme Apply + restart persistence, servers
  ping against a local fake status server) driven through Windows UI Automation and posted mouse messages. Fixes from
  that run (owner chose "English for now" over translating ZERO): **English-only UI** (`zero/ZeroLanguage.h`; Prism/Qt
  translations no longer mix in, dates en_US, Language settings hidden), Mod Health headline "Couldn't read any of the
  N mods" (neutral) instead of a green "No problems found" when nothing was readable + "Checked X of Y mods" + natural
  sort, search-field icon centred on every page (qss `QLineEdit QToolButton`), accessible clickable cards and palette
  rows, restored-over world goes to the Recycle Bin as `<folder> (replaced <date>)`, Crash Detective no-logs caption
  removed and the top-bar title keeps "Tools › Crash Detective" (search pill shrinks first).

## Build & test
- Build: `scripts/zero/build.sh` · Build + tests: `scripts/zero/build.sh test` · Run: `scripts/zero/run-dev.cmd`.
- Result at end of session 4: **45/45 ctest pass** + `FileSystem (without symlink cases): 25 passed, 0 failed`.
  Session 5: same (45/45 + 25/25), with new cases `findingsUseNaturalOrder`, `rowsAreAccessible`,
  `clickableCardIsAnAccessibleButton`, `restoreTrashesTheOldWorldUnderAReadableName`.
- Driving the real app without stealing focus (session 5): Windows UI Automation from PowerShell (`AutomationElement`,
  Invoke / SelectionItem / Value patterns; Qt exposes rail buttons, list items, the palette field and rows, cards) plus
  `PostMessage` WM_LBUTTONDOWN/UP for plain mouse clicks, and `PrintWindow` for captures. Modal dialogs (QMessageBox)
  are separate top-level windows: find them with `EnumWindows` by process id; a UIA call into the main window blocks
  while one is open. Status pings: a tiny local fake status server on 127.0.0.1 answers handshake/status/ping.
- `FileSystem` investigation: 12 of its 19 functions pass and now run in `build.sh test`. The 7 `test_link*` symlink
  cases are not run: on Windows without Developer Mode, symlinks need privileges, so `FS::create_link` launches
  `zerolauncher_filelink.exe` elevated (UAC prompt); the elevated process doesn't inherit the MSYS2 PATH (missing-DLL
  dialogs) and the test hangs. Fix options for later: enable Developer Mode on the test machine, or bundle DLLs next to
  the helper.
- After moving the repo, `build/` had to be reconfigured (CMakeCache pointed at the old path). If that happens again:
  delete `build/CMakeCache.txt build/CMakeFiles build/build.ninja build/.ninja_*` and rerun the build.
- If linking fails with "Permission denied" on `build/zerolauncher.exe`, a dev instance is still running: rename the
  exe (`build/zerolauncher.locked-<n>.exe`) instead of killing the owner's process. Old renamed exes can be deleted
  once no process uses them.
- Screenshot/evidence dev aids (`--zero-screenshot`, `ZERO_SCREENSHOT_*` incl. `_THEME`, `_TRANSITION`, `_THEME_TO`,
  `_LAUNCH_PHASE`, `_PALETTE`, `_SHORTCUTS`) are listed in `docs/zero/SHELL.md`. Always use `-d <temp dir>` and an
  absolute output path. Fake instances must have `instance.cfg` **with a `[General]` header** and a well-formed
  `mmc-pack.json`, otherwise an upstream debug `ASSERT(isRunning() == false)` in `Task.cpp:108` (a
  ComponentUpdateTask started twice) blocks the app. Debug builds only; upstream behaviour, not investigated further.

## Status
| Area | State | Verified by |
|---|---|---|
| Build baseline | ✅ MSYS2 Debug build, helper jars Java 8 | build + tests |
| Identity / data separation / Prism import | ✅ (session 1) | code review; import not run interactively |
| Credentials | ⚠️ dev builds reuse Prism's MSA client ID + CurseForge key; `-DZERO_RELEASE_BUILD=ON` refuses them | CMake guard |
| Theme engine v2 | ✅ `ThemeSpec` v2 (shape, bevel, texture, pixel roles, accent2/highlight), per-theme qss overlay, live switch, persisted via `ApplicationTheme` | `ZeroTheme`, `ZeroStyle` tests; Obsidian pixel-identical before/after |
| **Minecraft Theme** (`zero-minecraft`) | ✅ deepslate/stone/grass/diamond/gold palette, notched panels, bevelled block-press buttons, procedural stone texture, original **ZERO Pixel** font (TrueType built in memory), XP-bar launch sweep, pixel gem toasts | `ZeroPixelFont` test, screenshots `build/zero-v2-zero-minecraft-*.png`, `build/zero-v2-m3-*` |
| Motion system | ✅ `ZeroMotion` setting (system/on/off; **the owner's Windows has "Animation effects" off, so `system` = still: choose On in Appearance**), snapshot transitions (crossfade/rise/pixel dissolve), page rise, nav indicator glide, card lift, press ripple (Obsidian), toast slide/stack glide, theme transition, grid/list + details crossfades, dialog fade-in, native popup fades | `ZeroMotion` test (overlay lifetime, no leftover effects), evidence frames `build/zero-v2-motion-*-t*.png`; hover/press/toast motion **not** seen with real input |
| Play button | ✅ `ZeroPlayButton`: Idle → Launching (sweep / XP bar) → Running (hover = Stop) → failure shake; driven by `Application::launchPhaseChanged/launchEnded` (Preparing at controller start, Running when the game process starts) | `ZeroPlayButton` test, static screenshots; **no real Minecraft launch verified** |
| Appearance page | ✅ live preview of a theme before applying, theme cards, Apply (persists, transition plays), motion Follow Windows/On/Off, link to legacy appearance settings | screenshots |
| Shell v2 | ✅ `ZeroTopBar` (title/subtitle/breadcrumb, page actions, account chip, search pill), `ShellPage` chrome API, 60 px sidebar header (logo + wordmark), Appearance/More/Settings bottom group, Alt+1..7 | `ZeroShell` test, screenshots `build/zero-v2-m4-*.png` |
| Command palette | ✅ Ctrl+K / top-bar pill: instances (↵ Play, Ctrl+↵ show in Library), navigation, real QActions, settings pages, themes, motion; fuzzy ranking in `launcher/zero/palette/CommandMatcher`; shortcut sheet Ctrl+/ (also in More menu). Kill moved to **Ctrl+Shift+K**; Settings = **Ctrl+,** | `CommandMatcher`, `ZeroCommandPalette` tests, screenshots `build/zero-v2-m5-*.png` |
| **Instance Profiles / Library 2.0** | ✅ `launcher/zero/profiles/` (`InstanceProfile`, `LibraryFilter`), data in `instance.cfg` (`ZeroFavorite`, `ZeroTags`, `ZeroArtwork`) + `<instance>/.zero/artwork.png` written with `QSaveFile`; `FilterChipBar`, `ArtworkCache`, `FlowLayout`; chips filter through `InstanceProxyModel::setPredicate` (groups kept) | `InstanceProfile`, `LibraryFilter`, `ZeroLibrary` tests, screenshots `build/zero-v2-m6-*.png`; **not tried with real input** |
| **Session History** | ✅ one record per launch (played / stopped / crashed + exit code / failed) from `launchPhaseChanged` + `gameStoppedUnexpectedly` + `launchEnded`; JSON written with `QSaveFile`, cap 500, corrupt file moved to `.corrupt`; Home "Recent activity" | `SessionHistory` test, screenshots `build/zero-v2-m7-home-*.png` (hand-written history); **no real launch recorded yet** |
| **Worlds Hub** | ✅ read-only page: every world of every instance (`launcher/zero/worlds/WorldScanner`: level.dat via Prism `World` + `versionName()`/`isHardcore()`, async sizes on QThreadPool with generations, no link/junction following), search, instance filter, sort, details panel (seed copy, open folder, show instance) | `WorldScanner` test (symlink sub-case skipped: needs privileges; junction case runs), screenshots `build/zero-v2-m8-worlds-*.png`, fake worlds' files hashed unchanged after runs; **not tried with real input** |
| **Smart Backup** (worlds) | ✅ `launcher/zero/backup/` (`WorldBackup` pure functions, `WorldBackupJobs` runner): zips in `<data>/zero/backups/<instance>/<world>/` with a SHA-256 manifest, written as `.part` and verified before rename; restore = verify → safety snapshot → extract to `saves/.zero-restore-*` + verify → rename swap with rollback → old world to Recycle Bin; zip-slip rejected; retention 10 manual + 3 safety; refused while the instance runs and launch blocked during a job | `WorldBackup` test (66 rows incl. corruption, zip-slip, failed swap rollback, retention, free space), screenshots `build/zero-v2-m9-backups-*.png`; **restore/delete dialogs, toasts, Recycle Bin and the launch block never clicked through in the app** |
| **Auto backups** | ✅ `Kind::Auto` (keep 5/world): when a session where the game ran ends, worlds whose `level.dat` changed since the game started are backed up one at a time (>2 GB skipped + warned); instance launch-locked meanwhile; global toggle `ZeroAutoBackupWorlds` (default on) at the bottom of the Worlds backups section; one toast per outcome | `AutoBackup` test (real temp worlds + real jobs), `WorldBackup` test, screenshot `build/zero-v2-m10-autobackup.png`; **never triggered by a real game session** |
| **Storage Manager** | ✅ Tools › Storage: total + drive free space, 11 categories (no double counting, links/junctions not followed), per-instance rows (click → Library), Clean up: logs/crash reports > 30 days → Recycle Bin after confirmation, each file re-validated, never a permanent delete; world backups + interrupted-restore folders info only | `StorageScanner` test (28 cases), screenshots `build/zero-v2-m11-*.png`; **confirm dialog / real Recycle Bin move / toasts never clicked through** |
| **Java Doctor** | ✅ Tools › Java Doctor: pure `diagnose()` mirrors `AutoInstallJava` + `VerifyJavaInstall` (pinned → detected → download; configured path; 32-bit + >2 GB; ignored check); requirements read from `mmc-pack.json` + patches/meta files without loading or writing anything; per-instance status pill + "Use Java N" (confirmation, refused while running; writes `OverrideJavaLocation`/`JavaPath`/`AutomaticJava` like Prism's Java page); runtimes list; Crash Detective "wrong Java" links here | `JavaDoctor` test (36 cases), screenshots `build/zero-v2-m12-*.png`; **"Use Java N" never clicked through; no real launch after a change** |
| **Mod Intelligence** | ✅ own read-only parser (fabric/quilt json, mods.toml + neoforge.mods.toml, mcmod.info, manifest, nested jars from memory, depth ≤ 3); tri-state version matching (Fabric predicates, Maven ranges); findings Confirmed/Likely/Possible/Unknown: missing/mismatched deps, MC version, loader version, wrong loader (Connector / NeoForge 1.20.1-1.20.4 lower to Possible), breaks/conflicts, duplicates, unreadable jars; an unreadable jar lowers missing-dep to Possible. Tools › Mod Health with instance picker, Library "Check mods", palette "Check mods". No online/outdated checks | `ModHealth` test (159 cases incl. real zip jars), screenshots `build/zero-v2-m13-*.png`; **never run on a real modpack** |
| **Mod repair actions** | ✅ Confirmed wrong loader / MC version / loader version → "Disable mod…"; Confirmed duplicates → disable all but the unique newest (only when every version compares); preview dialog lists each `x.jar → x.jar.disabled`; never deletes, never overwrites an existing target, refused while running; Undo toast; "Fix N problems…"; Disabled mods card with Enable | `ModRepair` test (22 + 1 skipped symlink case), screenshots `build/zero-v2-m14-*.png`; **dialogs, Undo and Enable never clicked** |
| **Screenshot Gallery** | ✅ `launcher/zero/gallery/ScreenshotScanner` (read-only) + `ScreenshotsPage`: fluid 16:9 grid, thumbnails decoded on the pool at 400 px into an in-memory LRU (300), search / instance filter / sort persisted, in-window viewer (←/→, Esc, Copy, Open, Show in folder, Show instance, Delete → Recycle Bin after confirmation, never a permanent delete) | `ScreenshotScanner` test, screenshots `build/zero-v2-m15-*.png`; **viewer buttons, keys and delete never used with real input** |
| **Server Vault** | ✅ read-only `servers.dat` parser (own, on the bundled nbt lib), address normalization + grouping across instances, `ServerPinger` (McResolver/McClient, 4 in flight, 5 s timeout, session cache, motd flattening), `ServersPage` + details panel, Play on server = Prism's Join | `ServerVault` test (49 cases incl. pinger vs a fake local status server), screenshots `build/zero-v2-m16-servers-*.png`; **real quick-join launch, real servers and menu popups never exercised** |
| Crash Detective | ✅ (session 1) 13 rules; now opened under the Tools breadcrumb | `CrashDetective` test |

## Manual QA checklist (owner, real input needed)
Start `scripts/zero/run-dev.cmd`. For animations: Appearance → Motion → **On** (Windows animation effects are off here).
- [ ] Session-1 items still open: Prism import, Microsoft login/account switch, create vanilla + Fabric instance,
      rename/delete+Undo/groups/drag-and-drop/copy/export/shortcut/icon, context menu, settings dialog under Obsidian
- [ ] **Real launch**: Play shows Launching (sweep; XP bar in Minecraft) → Running when the game window appears →
      hover shows Stop → Stop kills (with confirmation). A failing launch shakes and shows "Launch failed"
- [ ] Theme switch from Appearance (preview first, then Apply): Obsidian → Minecraft dissolves in blocks, back crossfades;
      choice survives a restart; Prism's settings dialog theme combo stays in sync
- [ ] Motion On: page rise, nav pill glide, card lift on hover, ripple on press (Obsidian), toasts slide/stack, grid↔list
      crossfade, dialog fade-in — check for flicker (dialog fade can be removed in `fadeDialogIn`, `ZeroTheme.cpp`)
- [ ] Ctrl+K palette: type, arrows, ↵ launches, Ctrl+↵ shows in Library, Esc returns focus; Ctrl+/ sheet; Alt+1..7;
      Ctrl+Shift+K kills; Ctrl+, opens settings
- [ ] Top bar: Library actions (grid/list, New instance), search/sort row, account menu opens below the chip
- [ ] Crash Detective: force a crash → toast → Diagnose → breadcrumb "Tools › Crash Detective" → back via "Tools"
- [ ] Library 2.0: star on card hover toggles favorite without selecting; chips (Favorites / Running / loader / #tag) filter
      grid and list, clicking the active chip returns to All; add/remove tags (Enter, Esc, Backspace), description survives
      switching instance and restart; Set artwork… (png/jpg/webp) shows on card, details and Home hero; Remove artwork
- [ ] Session History: launch, play, quit → Home "Recent activity" shows "Played for …"; Stop from the launcher → "Stopped
      after …"; a crash → red row with exit code + Diagnose opens Crash Detective; row click opens the instance in Library
- [ ] Worlds: Alt+3 opens it; worlds of real instances listed with icon/version/size; search, instance filter, sort
      persist; Ctrl+F / Esc; Seed copy toast; Open folder; Show instance in Library; Refresh; nothing inside saves changes
- [ ] Smart Backup (use a throwaway world first!): Back up now → toast + row; Restore → confirm → world replaced, a
      Safety row appears, the game opens the restored world; Delete → Recycle Bin; buttons disabled while the game runs;
      Play blocked with a message while a backup runs (large world); leftover `saves/.zero-*` folders only after a crash
- [ ] Auto backups: play a world, quit → toast "Backed up 1 world(s) of …", an **Auto** row in Worlds; untouched worlds get
      none; Play is blocked while it runs; unticking "Back up automatically after playing" stops it
- [ ] Storage (Tools › Storage or Ctrl+K "Open Storage"): numbers look plausible vs Explorer; instance row opens Library;
      Move old logs to Recycle Bin → confirm → toast, files in the Recycle Bin, list rescans; Rescan button
- [ ] Java Doctor (Tools › Java Doctor): statuses match what really happens at launch (try an instance with a wrong Java);
      "Use Java N" → confirm → instance settings show the new path; Java settings button opens the Java page
- [ ] Mod Health on a real modpack (Fabric and Forge/NeoForge): no false Confirmed findings; try removing a dependency
      (e.g. fabric-api) → Confirmed missing; Library "Check mods" opens it for that instance
- [ ] Mod repair: "Disable mod…" → preview → file renamed to `.disabled` → Undo restores it; "Fix N problems…";
      Enable in "Disabled mods"; refused while the game runs; Prism's Mods page agrees with the new state
- [ ] Screenshots (Alt+4): thumbnails of real screenshots, filter/sort persist; viewer ←/→/Esc, Copy, Open, Delete → Recycle Bin
- [ ] Servers (Alt+5): servers of real instances grouped across instances; "Responded" / "No response" pills (and the note that a response is not availability); sort by Players / Latency; **Play on server** launches the instance and joins (with several instances: menu; disabled while the instance runs); Copy address toast; Refresh re-pings; a real quick join was never run
- [ ] Window < 1200 px collapses the rail; 1100×700 minimum

## Known limitations / open issues
- Prism public infrastructure still used: meta server, wiki/help URLs, translations, Prism-branded login success page.
  Release blockers in `docs/zero/CREDENTIALS.md`. `curseforge:`/`modrinth:` handlers shared with Prism.
- No updater, installer not built (NSIS template rebranded only; windeployqt not set up).
- Library details panel: its own "● Running" badge also shows while Preparing (LaunchTask sets running early).
- **English only**: ZERO has no translations yet, so every language setting is ignored (see `docs/zero/ARCHITECTURE.md`,
  "English only for now"). Native Windows dialogs (file pickers) still follow the OS language.
- Without a Microsoft account the Quick Setup wizard (login page) shows on every start (upstream Prism behaviour).
- At 1100×700 the Library details panel scrolls ("Delete" below the fold); Appearance page scrolls at 1280×800.
- `ZeroCard` hover shadow is drawn inside the card (a widget can't paint outside its rect); grid cards get a real lift.
- Crash Detective regexes are validated only on self-authored fixtures (see session-1 notes in git history).
- Minecraft Theme: pixel font is used for wordmark, page titles and overlines only; legacy Prism dialogs get the
  palette/qss but no pixel details.

## Next tasks (priority order)
1. ~~Instance Profiles + Library 2.0~~ done in session 3. Left out: bulk actions (multi-select tag/favorite/move),
   "recently added" sort, favorite column in the list view; Home hero still = most recently played.
2. ~~Session History~~ done in session 3 (Home "Recent activity"). Not done: per-instance history view, stats.
3. ~~Worlds Hub~~ and ~~Smart Backup (manual + restore)~~ done in session 3, ~~automatic after playing~~ session 4.
   Smart Backup left: scheduled backups, per-instance auto opt-out, instance backup/restore, pre-update snapshots, restore preview, import/export of worlds;
   backup rows show only relative time (several "Just now" look alike; full date in the tooltip). Empty folders are not stored.
4. ~~Storage Manager~~ (session 4; left: "find large files", scan progress). ~~Java Doctor~~ (session 4). ~~Mod Intelligence~~ (session 4; left: outdated checks via Modrinth/CurseForge). Repair Center:
   first slice = Mod Health repair actions (session 4). Downloads/Update center, Repair Center, Discover, ~~Server Vault~~ (session 4; left: per-server palette "Join" entries, adding/editing servers),
   ~~Screenshot Gallery~~ (session 4) (Phases C–E in MISSION.md).
5. Release engineering: windeployqt + NSIS, own MSA client ID + CurseForge key, updater feed.

**Recommended start for session 5:** run `scripts/zero/build.sh test`, skim this file and `docs/zero/SHELL.md`, then
`/savvy-flow`: the manual QA of session 4's features with real input is the most valuable next step (owner), then
more Repair Center actions (Java reset to automatic, leftover cleanup with preview), Downloads/Update center, Discover.
