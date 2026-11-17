#!/usr/bin/env bash
# ZERO Launcher: refresh the Spanish translation file from the sources (Windows, MSYS2 UCRT64, Git Bash at the repo root).
#
#   scripts/zero/i18n-update.sh
#
# Scans launcher/ with lupdate and updates launcher/resources/zero/i18n/zero_es.ts: new strings are added as "unfinished", strings that
# are gone are dropped (-no-obsolete), every translation that still matches is kept. Then prints how many messages are finished and
# unfinished, split by "Zero::" contexts (ZERO's own classes) and the rest (upstream screens ZERO changed). Details: docs/zero/I18N.md.
set -euo pipefail
cd "$(dirname "$0")/../.."

export PATH="/c/msys64/ucrt64/bin:$PATH"
# lupdate.exe in ucrt64/bin needs Qt6Qml.dll (qt6-declarative, not installed here); the static build in qt6-static/bin runs as it is.
LUPDATE=/c/msys64/ucrt64/qt6-static/bin/lupdate.exe
[ -x "$LUPDATE" ] || LUPDATE=lupdate
TS=launcher/resources/zero/i18n/zero_es.ts
mkdir -p "$(dirname "$TS")"

log=$(mktemp)
trap 'rm -f "$log"' EXIT
# -locations none: no file names or line numbers in the .ts (they would change with every edit and say nothing about the translation).
"$LUPDATE" launcher -no-obsolete -sort-messages -locations none -disable-heuristic sametext -disable-heuristic similartext -source-language en_US -target-language es -ts "$TS" > "$log" 2>&1 || {
  cat "$log"
  echo "lupdate failed" >&2
  exit 1
}

# Warnings about ZERO's own sources point at strings that will not be found at run time (e.g. a class without Q_OBJECT): show them.
ours=$(grep -E "(launcher/zero/|launcher/ui/zero/).*(lacks Q_OBJECT|Cannot invoke|Unbalanced|Parse error)" "$log" || true)
if [ -n "$ours" ]; then
  echo "lupdate warnings in ZERO sources (fix them):"
  echo "$ours" | sed 's#.*launcher/#  launcher/#'
fi
[ "${VERBOSE:-0}" = 1 ] && grep -v -E "^(Scanning|Updating)" "$log" || true
grep -E "Found [0-9]+ source text" "$log" | sed 's/^ *//'

awk '
  /<name>/ { ctx = $0; sub(/.*<name>/, "", ctx); sub(/<\/name>.*/, "", ctx); grp = (ctx ~ /^Zero::/) ? "Zero::" : "other"; next }
  /<translation/ {
    if ($0 ~ /type="(vanished|obsolete)"/) next
    total[grp]++
    if ($0 ~ /type="unfinished"/) unfinished[grp]++; else finished[grp]++
  }
  END {
    printf "%-10s %8s %10s %12s\n", "contexts", "messages", "finished", "unfinished"
    n = 0; f = 0; u = 0
    for (g in total) {
      printf "%-10s %8d %10d %12d\n", g, total[g], finished[g], unfinished[g]
      n += total[g]; f += finished[g]; u += unfinished[g]
    }
    printf "%-10s %8d %10d %12d\n", "all", n, f, u
  }' "$TS"
echo "updated $TS"
