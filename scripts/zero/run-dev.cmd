@echo off
rem Double-clickable launcher for the ZERO dev build.
rem The dev build links against the MSYS2 UCRT64 runtime and Qt DLLs in C:\msys64\ucrt64\bin,
rem which are not on the normal Windows PATH, so we prepend them here.
setlocal
set "ROOT=%~dp0..\.."
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
if not exist "%ROOT%\build\zerolauncher.exe" (
  echo ZERO dev build not found at build\zerolauncher.exe
  echo Build it first from Git Bash: scripts/zero/build.sh
  pause
  exit /b 1
)
start "" "%ROOT%\build\zerolauncher.exe" %*
