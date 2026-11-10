# ZERO Launcher — Handoff

Last updated: 2026-11-10 · Branch `zero-development` (local only, never pushed) · Base: Prism Launcher `develop` @ 00f216ce5

## Read first
`CLAUDE.md` (commands, hard rules) → `docs/zero/ARCHITECTURE.md` → `docs/zero/DESIGN.md` → `docs/zero/SHELL.md` → `filemap.md`.

## Environment (this machine)
- Toolchain: MSYS2 at `C:\msys64`, UCRT64 packages (GCC, CMake, Ninja, ccache, Qt 6.12, cmark, libarchive, qrencode,
  tomlplusplus, zlib, ECM, librsvg, imagemagick). JDK 21 (Temurin) on PATH builds the helper jars (`--release 8`).
- Build: `scripts/zero/build.sh test`. Run: `scripts/zero/run-dev.cmd`. First full build ≈ 15 min, incremental 1–3 min.
- The owner's real Prism install lives at `%APPDATA%\PrismLauncher`. Never write to it. Automated runs always use `-d <temp>`.
- The upstream `FileSystem` test is excluded: it spawns `zerolauncher_filelink.exe` elevated (UAC), which hangs and
  shows missing-DLL dialogs because the elevated process doesn't get the MSYS2 PATH.

## Status
| Area | State | Verified by |
|---|---|---|
| Build baseline | ✅ builds with MSYS2; helper jars target Java 8 | build + tests |
| Identity | ✅ ZERO Launcher / `zerolauncher.exe` / AppID `app.zerolauncher.ZEROLauncher` / v0.1.0 / own data dir, config, logs, installer keys, URL scheme, icon | exe version resource, code review |
| Prism import | ✅ copy-only, first run only, asks first, never with `-d`/env dir, absolute paths rebased | code review (not run interactively) |
| Credentials | ⚠️ dev builds reuse Prism's MSA client ID + CurseForge key; `-DZERO_RELEASE_BUILD=ON` refuses them | CMake guard tested |
| Design system | ✅ Obsidian tokens, generated QSS, ZERO theme default, components, Lucide icons | `ZeroStyle` test + `build/zero-gallery.png` |
| Shell | ✅ nav rail, Home, Library (grid/list/search/details), dark title bar, live running state | screenshots `build/zero-shell-*.png`, tests |
| Crash Detective | ✅ 13 rules (Java version, OOM, missing deps, incompatible versions, duplicates, mixins, suspected mods, GPU driver, ticking entity, config, wrong loader, JVM native crash, exit codes) with explicit Confirmed/Likely/Possible policy; Tools page; "stopped unexpectedly" toast → Diagnose | `CrashDetective` test (20 fixtures), screenshots `build/zero-tools-*.png`; crash hook compiled but not triggered by a real crash |

**Not yet exercised interactively** (no GUI automation available in the sessions so far): keyboard shortcuts with hidden
toolbars, context menu, drag-and-drop between groups, inline rename, launching Minecraft from the new pages, Microsoft
login. First thing to do in a session with a human: run `scripts/zero/run-dev.cmd` and click through these.

## Known limitations / decisions to revisit
- Prism public infrastructure still used: meta server (`meta.prismlauncher.org`), wiki/help URLs, translations,
  login "success" page (Prism-branded). Release blockers listed in `docs/zero/CREDENTIALS.md`.
- `curseforge:`/`modrinth:` URL handlers are shared with Prism (last installed wins).
- No updater (no ZERO release infrastructure yet; `Launcher_UPDATER_GITHUB_REPO` empty).
- Installer: the NSIS template is rebranded but has not been built (NSIS + DLL bundling via windeployqt not set up).
  The dev build only runs with MSYS2 DLLs on PATH.
- Text renders zeros slashed with Segoe UI Variable on this machine (font feature); cosmetic, matches the logo.
- Dropped Prism UI: cat background, news toolbar, theme/lock-toolbar/status-bar toggles (settings still exist).
- `ZeroStyle` real-pointer hover check is opt-in (`ZERO_TEST_REAL_POINTER=1`).
- Crash Detective regexes were written from known Fabric/Forge/NeoForge formats and self-authored fixtures; validate
  against real crash logs (least certain: Fabric duplicate-mod and "conflicts with" phrasing). Only the newest
  latest.log / crash report / hs_err are scanned; Fabric silently ignoring a Forge jar is not detectable.

## Next tasks (priority order)
1. **Interactive QA pass** of the shell with the owner (list above), fix regressions.
2. **Instance Profiles**: favorites (Home hero prefers favorite), tags, notes, custom artwork; extend instance.cfg
   via `BaseInstance::settings()` rather than a parallel store. Library filter chips for tags/favorites.
3. **Command Palette** (Ctrl+K): instances, pages, actions, settings; shortcut reference sheet.
4. **Smart Backup** (`launcher/zero/backup/`): world/instance snapshots (zip or dedup store), retention, restore
   preview, pre-update snapshot hook, never overwrite on restore without a safety snapshot. Then **Worlds** page.
5. **Downloads center**: surface Prism's task system (`Task`, `ConcurrentTask`, `NetJob`) in a page; history.
6. **Java Doctor**: wrap `JavaInstallList`/`JavaChecker` + Minecraft version → required Java mapping (shared with
   Crash Detective rule 1).
7. **Storage Manager**, **Mod Health Inspector**, **Discover** page (reuse ResourceDownloadDialog/modplatform code),
   **Theme Studio** (edit `Zero::ThemeSpec`), optional **Ollama** explanation for Crash Detective.
8. **Release engineering**: windeployqt + NSIS installer build script, own MSA client ID + CurseForge key, updater feed.
