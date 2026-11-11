# ZERO Launcher — Handoff

Last updated: 2026-11-10 (session 3, Mission 002) · Branch `zero-development` (local only, never pushed) ·
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

## Build & test
- Build: `scripts/zero/build.sh` · Build + tests: `scripts/zero/build.sh test` · Run: `scripts/zero/run-dev.cmd`.
- Result at end of session 3: **35/35 ctest pass** + `FileSystem (without symlink cases): 25 passed, 0 failed`.
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
| Shell v2 | ✅ `ZeroTopBar` (title/subtitle/breadcrumb, page actions, account chip, search pill), `ShellPage` chrome API, 60 px sidebar header (logo + wordmark), Appearance/More/Settings bottom group, Alt+1..4 | `ZeroShell` test, screenshots `build/zero-v2-m4-*.png` |
| Command palette | ✅ Ctrl+K / top-bar pill: instances (↵ Play, Ctrl+↵ show in Library), navigation, real QActions, settings pages, themes, motion; fuzzy ranking in `launcher/zero/palette/CommandMatcher`; shortcut sheet Ctrl+/ (also in More menu). Kill moved to **Ctrl+Shift+K**; Settings = **Ctrl+,** | `CommandMatcher`, `ZeroCommandPalette` tests, screenshots `build/zero-v2-m5-*.png` |
| **Instance Profiles / Library 2.0** | ✅ `launcher/zero/profiles/` (`InstanceProfile`, `LibraryFilter`), data in `instance.cfg` (`ZeroFavorite`, `ZeroTags`, `ZeroArtwork`) + `<instance>/.zero/artwork.png` written with `QSaveFile`; `FilterChipBar`, `ArtworkCache`, `FlowLayout`; chips filter through `InstanceProxyModel::setPredicate` (groups kept) | `InstanceProfile`, `LibraryFilter`, `ZeroLibrary` tests, screenshots `build/zero-v2-m6-*.png`; **not tried with real input** |
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
- [ ] Ctrl+K palette: type, arrows, ↵ launches, Ctrl+↵ shows in Library, Esc returns focus; Ctrl+/ sheet; Alt+1..4;
      Ctrl+Shift+K kills; Ctrl+, opens settings
- [ ] Top bar: Library actions (grid/list, New instance), search/sort row, account menu opens below the chip
- [ ] Crash Detective: force a crash → toast → Diagnose → breadcrumb "Tools › Crash Detective" → back via "Tools"
- [ ] Library 2.0: star on card hover toggles favorite without selecting; chips (Favorites / Running / loader / #tag) filter
      grid and list, clicking the active chip returns to All; add/remove tags (Enter, Esc, Backspace), description survives
      switching instance and restart; Set artwork… (png/jpg/webp) shows on card, details and Home hero; Remove artwork
- [ ] Window < 1200 px collapses the rail; 1100×700 minimum

## Known limitations / open issues
- Prism public infrastructure still used: meta server, wiki/help URLs, translations, Prism-branded login success page.
  Release blockers in `docs/zero/CREDENTIALS.md`. `curseforge:`/`modrinth:` handlers shared with Prism.
- No updater, installer not built (NSIS template rebranded only; windeployqt not set up).
- Library details panel: its own "● Running" badge also shows while Preparing (LaunchTask sets running early).
- Library search field's leading icon sits ~8 px low (pre-existing).
- At 1100×700 the Library details panel scrolls ("Delete" below the fold); Appearance page scrolls at 1280×800.
- `ZeroCard` hover shadow is drawn inside the card (a widget can't paint outside its rect); grid cards get a real lift.
- Crash Detective regexes are validated only on self-authored fixtures (see session-1 notes in git history).
- Minecraft Theme: pixel font is used for wordmark, page titles and overlines only; legacy Prism dialogs get the
  palette/qss but no pixel details.

## Next tasks (priority order)
1. ~~Instance Profiles + Library 2.0~~ done in session 3. Left out: bulk actions (multi-select tag/favorite/move),
   "recently added" sort, favorite column in the list view; Home hero still = most recently played.
2. **Session History** (launch date/duration/exit status from `launchEnded` + playtime) → Home "Recent activity".
3. **Worlds Hub** (read-only discovery: level.dat via the bundled NBT lib, icon.png, size, dates, instance) then
   **Smart Backup** (`launcher/zero/backup/`: zip snapshots with manifest + hash verification, retention, restore with a
   safety snapshot first, pre-update hook).
4. Downloads/Update center, Storage Manager, Java Doctor, Mod Intelligence, Repair Center, Discover, Server Vault,
   Screenshot Gallery (Phases C–E in MISSION.md).
5. Release engineering: windeployqt + NSIS, own MSA client ID + CurseForge key, updater feed.

**Recommended start for session 4:** run `scripts/zero/build.sh test`, skim this file and `docs/zero/SHELL.md`, then
`/savvy-flow` task 2 (Session History → Home "Recent activity").
