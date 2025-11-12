#!/usr/bin/env bash
# Build arvik, archive two files, extract them into a clean dir, and diff.
# Needs libmd (Linux): sudo apt-get install libmd-dev
set -euo pipefail
cd "$(dirname "$0")"
make >/dev/null
BIN="$PWD/arvik-md4"

work=$(mktemp -d)
cd "$work"
echo "hello arvik" > one.txt
printf 'line one\nline two\n' > two.txt
mkdir orig && cp one.txt two.txt orig/

"$BIN" -cvf store.arvik one.txt two.txt
rm -f one.txt two.txt
"$BIN" -xvf store.arvik

rc=0
diff orig/one.txt one.txt || rc=1
diff orig/two.txt two.txt || rc=1
cd /; rm -rf "$work"

if [ "$rc" -eq 0 ]; then echo "PASS: archive then extract round-trips"; else echo "FAIL: extracted files differ"; fi
exit $rc
