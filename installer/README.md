<!-- SPDX-License-Identifier: GPL-3.0-only -->
# ZERO Setup (`installer/`)

ZERO's own installer / uninstaller. One `ZEROLauncher-<ver>-Windows-Setup.exe` that starts instantly, animates in the
Minecraft theme, installs with no internet, updates an existing install (made by this installer or by the old NSIS one)
and puts a same-looking uninstaller in the install folder. It replaces `program_info/win_install.nsi.in` (kept as the
fallback: `scripts/zero/package.sh --nsis`).

Standalone CMake project (C++20, Qt 6 Widgets only, no link to launcher code). It compiles exactly one launcher source,
`launcher/ui/zero/ZeroPixelFont.cpp` (the ZERO Pixel font, Qt only). Design tokens: `src/SetupTokens.h` (Minecraft Theme of
`docs/zero/DESIGN.md`, motion durations from the installer brief). The 16x16 mark in `src/MarkData.h` is generated from
`program_info/zero-logo-minecraft.svg` by `tools/gen_mark.py`.

## Build

```
installer/build.sh dev            # shared Qt  -> installer/build-dev/      (needs C:\msys64\ucrt64\bin on PATH to run)
installer/build.sh dev test       # + unit tests (QtTest text log in build-dev/tests.log)
installer/build.sh release        # static Qt  -> installer/build-release/  (one exe, only Windows DLLs)
```
`release` needs `pacman -S --needed mingw-w64-ucrt-x86_64-qt6-static` (prefix `C:\msys64\ucrt64\qt6-static`); without it the
script stops with that message. Outputs: `zero-setup.exe` (the stub), `zero-setup-pack.exe`, `ZeroSetupTests.exe` (dev).
Static notes (see `CMakeLists.txt`): only the Windows platform plugin is linked (`QT_DEFAULT_PLUGINS 0`), `.a` archives are
preferred over import libraries, harfbuzz's imported target is repointed to `libharfbuzz.a`.

`scripts/zero/package.sh` builds the release stub, runs
`zero-setup-pack --stub zero-setup.exe --stage dist/stage/ZEROLauncher --version X.Y.Z --out dist/ZEROLauncher-X.Y.Z-Windows-Setup.exe`
and checks that the stub imports only Windows DLLs. Static Qt missing -> clear notice + the NSIS installer.
`zero-setup-pack --info <setup.exe>` lists a payload and verifies its checksum.

## How it works

* **Payload.** `zero-setup-pack` appends the launcher folder to the stub exe. The setup reads its own file: trailer ->
  manifest -> data. Nothing is read at start-up except the trailer and the manifest, so the window opens at once.
* **Install** (`src/core/Installer.cpp`): verify the whole payload SHA-256 -> check free space -> extract every file into
  `<target>.zero-setup-tmp/` verifying size + SHA-256 per file -> stage `uninstall.exe` (the stub, i.e. the exe without payload) and
  `.zero-install.json` (version + list of owned files) -> swap in with a journal (each replaced file is renamed into
  `<target>.zero-setup-old/`, new files are renamed in; files the previous version owned and the new one lacks are moved aside
  too) -> on any failure the journal is rolled back, so a failed or cancelled install leaves the previous install untouched.
  Cancel works until the swap starts (the swap is a few hundred renames). Foreign files in the folder are never touched.
* **Upgrade** (`Upgrade.cpp`): the install folder is `HKCU\Software\ZEROLauncher\InstallDir` (like NSIS `InstallDirRegKey`), else
  `%LOCALAPPDATA%\Programs\ZEROLauncher`. A folder with `.zero-install.json` is a ZERO Setup install; a folder with
  `zerolauncher.exe` and no manifest is an NSIS install (its layout is the NSIS file list). The Welcome screen then says
  "ZERO <old> is installed. Update to <new>." and the button reads **Update**.
* **ZERO running**: Setup tries to open the exes for writing; if that fails it asks to close ZERO (Retry / Cancel). It never
  kills the process. Silent mode waits up to 60 s, then exits with code 2.
* **Uninstall** (`Uninstaller.cpp`): `uninstall.exe --uninstall` (also what a double click does when it has no payload and sits in
  an install). It copies itself to `%TEMP%\zero-uninstall-XXXXXX\` and runs from there (so it can delete itself too), removes
  registry/shortcuts, the files listed in `.zero-install.json` (NSIS installs: the NSIS layout), empty folders and the install folder
  when empty. "Also move my worlds and settings to the Recycle Bin" is **off** by default; only when ticked `%APPDATA%\ZEROLauncher` is moved
  to the Recycle Bin (`ISystem::moveToRecycleBin`, `QFile::moveToTrash`; never a permanent delete, no fallback). That happens
  first: if the Recycle Bin refuses, nothing else is removed and the error reads "Your worlds and settings were not removed;
  they are still in <path>." The folder is refused unless it is named `ZEROLauncher`, is not around/inside the install folder
  and is never a Prism folder.
  Files that cannot be deleted are scheduled with `MoveFileEx(DELAY_UNTIL_REBOOT)`. The temp copy removes itself via a delayed `cmd`.
* **Motion** is on by default, whatever the Windows "Show animations" setting says (same policy as the launcher, where motion
  defaults to On). `ZERO_SETUP_REDUCE_MOTION=1` is the only switch that turns it off (no drops, twinkles, bob or slides: instant
  states); the tests use it.

## Command line

Both `--name` and the NSIS style `/Name` work; the updater (`launcher/updater/prismupdater`) starts the setup with no switches.

| Switch | Meaning |
|---|---|
| `/S`, `--silent` | no window; exit 0 ok, 1 failed, 2 ZERO is running |
| `/D=<dir>` (last argument), `--target <dir>` | install folder |
| `/NoShortcuts` | no Start menu / desktop shortcut (NSIS `/NoShortcuts`) |
| `/NoUninstaller` | no uninstall entry, no `uninstall.exe` (NSIS `/NoUninstaller`) |
| `--shell-assoc` | the optional "Open With" component (NSIS: off by default, no UI in Setup) |
| `--uninstall` (`_?=<dir>`, `--no-relaunch`) | uninstall; `_?=` / `--no-relaunch` = run in place, no temp copy |
| `--delete-data` | uninstall: also move `%APPDATA%\ZEROLauncher` to the Recycle Bin |
| `--no-system` | registry, shortcuts, URL handlers and the Recycle Bin move are only simulated (fake system); the plan goes to `--plan <file>` (default `%TEMP%\zero-setup-plan.txt`) |
| `--screenshot <screen> <png> [--scale 1.5]` | render a screen headless, no side effects: `welcome`, `drop:40`, `options`, `update`, `update-options`, `installing:45`, `done`, `error`, `running`, `stop`, `uninstall`, `uninstalling:60`, `uninstall-done` |
| `--demo`, `--demo-fail` | whole UI with a fake slow install (nothing written; `--demo-fail` fails half way) |

Git Bash converts `/S` into a path: use `MSYS2_ARG_CONV_EXCL='*'`. The exe is a GUI program, so `cmd`/PowerShell do not wait for
it: use `start /wait` / `Start-Process -Wait`.

## System footprint (reproduced from `win_install.nsi.in`, per-user, never needs admin)

| What | NSIS | ZERO Setup |
|---|---|---|
| Default folder | `$LOCALAPPDATA\Programs\ZEROLauncher`, `InstallDirRegKey HKCU\Software\ZEROLauncher InstallDir` | same |
| Execution level | `user` (everything in HKCU) | same (`asInvoker`) |
| Files | exe, `_filelink.exe`, `_updater.exe`, `qt.conf`, `qtlogging.ini`, `*.dll`, `iconengines imageformats jars multimedia platforms styles tls` | the whole `dist/stage/ZEROLauncher` folder (same set) + `uninstall.exe` + `.zero-install.json` |
| `HKCU\Software\ZEROLauncher` | `InstallDir` | same |
| URL handlers | `HKCU\Software\Classes\{curseforge,modrinth,zerolauncher}`: `URL Protocol`="" and `shell\open\command` = `"<dir>\zerolauncher.exe" "%1"` (curseforge/modrinth are taken over) | same |
| Uninstall entry | `HKCU\...\Uninstall\ZEROLauncher`: DisplayName, DisplayIcon, UninstallString, QuietUninstallString, InstallLocation, Publisher "ZERO Launcher Contributors", Version (`X.Y.Z.0`), DisplayVersion, VersionMajor, VersionMinor (strings), EstimatedSize (KB, DWORD), NoModify=1, NoRepair=1 | same values; UninstallString = `"<dir>\uninstall.exe" --uninstall`, Quiet = `... --uninstall /S` (NSIS: `_?=<dir>` / `/S _?=<dir>`; Setup also accepts those) |
| Shortcuts | `$SMPROGRAMS\ZERO Launcher.lnk` (on), `$DESKTOP\ZERO Launcher.lnk` (**off** by default) | both on by default (brief), Options page unticks them; same target / icon / start folder |
| Open-With (`.mrpack` default, `.zip` Open-With list, ProgID `ZEROLauncher.App`, `HKCU\Software\Classes\Applications\zerolauncher.exe`) | optional component, off | `--shell-assoc` only (no UI); uninstall always tears it down (same NSIS logic, restores the backed-up class) |
| Uninstall | removes both keys, files, shortcuts, `zerolauncher` ProgID, the curseforge / modrinth keys **only while they still equal `"<dir>\zerolauncher.exe" "%1"`**, then the Open-With teardown | same, same condition |
| Running app | `TaskKill /F` | asks the user to close it (Retry / Cancel), never kills |
| Previous version | runs the old uninstaller first | updates in place (journalled swap; stale owned files removed) |
| Portable marker | none (portable zip adds `portable.txt` itself) | none: Setup never writes or deletes `portable.txt` / `UserData` |
| `multimedia` folder | `File /nonfatal /r` | part of the payload when the stage has it |

## Payload format (little endian)

```
exe = stub bytes | payload | trailer(64)
payload = "ZSP1" | u32 manifestLen | manifest JSON | file data...
manifest = {"format":1,"version":"0.1.0","files":[{"path":"a/b.dll","size":123,"sha256":"<64 hex>"} ...]}   (sorted by path)
file data = for every file in manifest order, chunks until `size` bytes: u32 compLen | qCompress(<=1 MiB raw)  (zero-size files: no chunk)
trailer = "ZEROSETUPPAYLOAD"(16) | u64 payloadOffset | u64 payloadSize | sha256(payload)(32)
```
Rejected on open: bad magic / bounds (`offset + size + 64 != file size`), bad JSON or format, paths that are not plain
relative `/`-separated names (absolute, drive letter / `:`, backslash, `.` / `..` / empty segment, trailing dot or space, DOS device
names), duplicate (case-insensitive) paths, a file used as a folder. Extraction re-checks every path against the target
folder, limits chunk sizes, verifies size + SHA-256 per file and that no data follows the last file. The whole-payload hash is
checked before anything is written.

## Tests

`installer/build.sh dev test` -> `tests/SetupTests.cpp` (QtTest, fake registry `MemorySystem`, temp folders): pack/unpack round trip,
corrupt byte, truncated file / trailer, hostile paths, failed extraction / cancel / commit failure leave the target untouched,
upgrade over a fake NSIS install and over a V2 install, footprint = NSIS table, URL handlers removed only when they still point at
ZERO, uninstall keeps the data folder by default, moves it to the Recycle Bin (through the fake) only when asked, refuses unsafe data folders and stops without removing anything when the Recycle Bin refuses, jobs with `--no-system`, command line.
Manual: `--demo`, `--screenshot ...`, and `--target <scratch> --no-system` with a real payload (never run a real install on a
machine that has a Prism you care about: the real run takes over `curseforge://` / `modrinth://`).

## Known limits

* The stub is a static Qt app (~35 MB; Qt Core/Gui/Widgets are ~26 MB of it and OpenSSL's libcrypto 4.7 MB is pulled in by Qt Core itself, so
  `-Os` / section GC of our own code saves under 1 %), so the Setup exe is ~35 MB bigger than the payload and the installed `uninstall.exe` is
  as big as the stub.
* Not Authenticode-signed (SmartScreen will warn like with any unsigned installer).
* English only (like the launcher).
