# ZERO Launcher (fork of Prism Launcher, GPL-3.0-only)

Stack: C++20, Qt 6 (Widgets), CMake + Ninja, MSYS2 UCRT64 GCC toolchain at `C:\msys64`. Windows 10/11 is the only shipping target.

## Commands (Git Bash, repo root)
- Build: `scripts/zero/build.sh` (configures `build/` on first run; Debug, ccache)
- Build + unit tests: `scripts/zero/build.sh test` (runs `FileSystem` without its 7 symlink cases, which need an elevated helper and hang)
- Raw tools need `export PATH="/c/msys64/ucrt64/bin:$PATH"` first. Never use the vcpkg CMake preset.
- Run dev build: `scripts/zero/run-dev.cmd` (adds MSYS2 DLLs to PATH). For automated runs always pass
  `-d <temp dir>` so the real user data dir is never touched. Fake instances need `instance.cfg` with a `[General]`
  header (else an upstream debug ASSERT hangs the app). Never kill a running `zerolauncher.exe`; rename a locked exe.

## Hard rules
- `%APPDATA%\PrismLauncher` on this machine is the owner's real Prism install: never write to, delete or launch against it.
- Never commit or push unless the orchestrator asks. Never put secrets/tokens in logs or source.
- Keep upstream license headers intact in existing files; new ZERO files get the SPDX GPL-3.0-only header.
- Design: follow `docs/zero/DESIGN.md`. Colors/spacing come from `launcher/ui/zero/ZeroTokens.h`; no hex literals in widgets.
- No mock functionality: hide unfinished UI instead of stubbing it.
- New sources must be added to `launcher/CMakeLists.txt`; new tests to `tests/CMakeLists.txt` (`ecm_add_test`).

## Docs
`HANDOFF.md` (status, next steps) · `docs/zero/MISSION.md` (product brief) · `docs/zero/DESIGN.md` · `docs/zero/ARCHITECTURE.md` · `filemap.md`
