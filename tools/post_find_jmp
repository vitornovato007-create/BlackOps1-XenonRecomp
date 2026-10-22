#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="$ROOT/generated/ppc"
TOML_OUT="$ROOT/generated/BO1_switch_tables.toml"
RESULT="$ROOT/generated/jmp_candidates.txt"

echo "==> Procurando candidatas a setjmp e longjmp no C++ gerado"

awk '
function flush() {
    if (fn != "") {
        if (stores >= 10) printf "STORE %s %s %d %d\n", fn, curfile, stores, lines
        if (loads >= 10) printf "LOAD %s %s %d %d\n", fn, curfile, loads, lines
    }
    fn = ""; stores = 0; loads = 0; lines = 0
}
FNR == 1 { flush(); curfile = FILENAME; sub(".*/", "", curfile) }
/^PPC_FUNC_IMPL\(__imp__sub_/ {
    flush()
    fn = $0
    sub(/^PPC_FUNC_IMPL\(__imp__/, "", fn)
    sub(/\).*/, "", fn)
}
{ lines++ }
/^\tPPC_STORE_U64\(ctx\.r3\.u32 \+ -?[0-9]+, ctx\.r(1[4-9]|2[0-9]|3[01])\.u64\)/ { stores++ }
/^\tctx\.r(1[4-9]|2[0-9]|3[01])\.u64 = PPC_LOAD_U64\(ctx\.r3\.u32 \+ -?[0-9]+\)/ { loads++ }
/__imp__RtlUnwind\(ctx/ { printf "UNWIND %s %s\n", fn, curfile }
END { flush() }
' "$OUT_DIR"/ppc_recomp.*.cpp > "$RESULT" || true

count_calls() {
    local name="$1"
    grep -h -o "${name}(ctx, base)" "$OUT_DIR"/ppc_recomp.*.cpp | wc -l || true
}

echo
echo "==== INICIO JMP ===="

for kind in STORE LOAD; do
    if [[ "$kind" == "STORE" ]]; then
        echo "-- candidatas a setjmp (salvam r14..r31 num buffer apontado por r3) --"
    else
        echo "-- candidatas a longjmp (restauram r14..r31 de um buffer apontado por r3) --"
    fi

    found=0

    while read -r tag name file n lines; do
        if [[ "$tag" != "$kind" ]]; then
            continue
        fi

        found=$((found + 1))

        if [[ "$found" -le 12 ]]; then
            echo "$name ($file) registradores=$n linhas=$lines chamadas=$(count_calls "$name")"
        fi
    done < "$RESULT"

    if [[ "$found" -eq 0 ]]; then
        echo "(nenhuma)"
    elif [[ "$found" -gt 12 ]]; then
        echo "... e mais $((found - 12))"
    fi

    echo
done

echo "-- funcoes que chamam __imp__RtlUnwind --"
grep '^UNWIND' "$RESULT" | sort -u | head -n 20 || true
echo "==== FIM JMP ===="
echo

{
    echo "# ==== FILE: jmp_candidates.txt ===="
    head -n 500 "$RESULT" | sed 's/^/# /'
    echo "# ==== END FILE ===="
} >> "$TOML_OUT"
