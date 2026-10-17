#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

echo "BlackOps1-XenonRecomp: bootstrap validation"
echo "This script deliberately does not reuse MW2's executable addresses or generated PPC code."

XEX="${BO1_XEX:-${1:-}}"
if [[ -z "$XEX" ]]; then
  for candidate in game/default.xex private-input/default.xex; do
    if [[ -f "$candidate" ]]; then XEX="$candidate"; break; fi
  done
fi

if [[ -z "$XEX" || ! -f "$XEX" ]]; then
  cat >&2 <<'EOF'
ERROR: Black Ops 1 default.xex was not provided.

For local testing, set BO1_XEX=/path/to/your/legally-obtained/default.xex
or place it at game/default.xex (do not commit it).

The workflow must pass the executable privately. This bootstrap does not
download copyrighted game files.
EOF
  exit 2
fi

if [[ ! -f config/BO1.toml ]]; then
  cat >&2 <<'EOF'
ERROR: config/BO1.toml is not ready.

The MW2 config contains MW2-specific function addresses and jump-table data.
Those values cannot be safely renamed or copied for Black Ops 1. A BO1 config
must be derived from the exact executable and validated with XenonAnalyse.

No recompilation was attempted.
EOF
  exit 3
fi

if [[ ! -d XenonRecomp ]]; then
  cat >&2 <<'EOF'
ERROR: XenonRecomp source checkout is missing.
A pinned, reviewed XenonRecomp revision and BO1-specific patch set must be
added before this build can proceed.
EOF
  exit 4
fi

echo "Input and config files exist, but this bootstrap does not yet guarantee compatibility."
echo "STOP: implement and validate BO1-specific analysis/recompilation before building an APK."
exit 5
