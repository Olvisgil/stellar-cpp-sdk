#!/usr/bin/env bash
# Validate ISO 20022 fixture XML against the vendored XSDs (iso20022/xsd/).
# Dev-side check; requires xmllint (libxml2). Usage:
#   tools/check_iso20022_xsd.sh [file-or-dir ...]
# With no arguments it checks tests/fixtures/iso20022/.
set -euo pipefail
cd "$(dirname "$0")/.."

xsd_for() {  # pick the schema whose namespace the document declares
  local ns
  ns=$(grep -o 'xmlns="urn:iso:std:iso:20022:tech:xsd:[^"]*"' "$1" | head -1 \
       | sed 's/.*xsd:\([^"]*\)".*/\1/')
  [[ -n $ns && -f "iso20022/xsd/${ns}.xsd" ]] || { echo ""; return; }
  echo "iso20022/xsd/${ns}.xsd"
}

check() {
  local f=$1 xsd
  xsd=$(xsd_for "$f")
  if [[ -z $xsd ]]; then
    echo "SKIP  $f (no vendored schema for its namespace)"
    return 0
  fi
  if xmllint --noout --schema "$xsd" "$f" >/dev/null 2>&1; then
    echo "PASS  $f"
  else
    echo "FAIL  $f"
    xmllint --noout --schema "$xsd" "$f" 2>&1 | head -10
    return 1
  fi
}

rc=0
if [[ $# -gt 0 ]]; then
  files=("$@")
else
  files=(tests/fixtures/iso20022/*.xml)
fi
for f in "${files[@]}"; do check "$f" || rc=1; done
exit $rc
