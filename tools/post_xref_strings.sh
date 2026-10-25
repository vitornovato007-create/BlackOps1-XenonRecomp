#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="$ROOT/generated/ppc"
TOML_OUT="$ROOT/generated/BO1_switch_tables.toml"
LOG="$ROOT/generated/xenonanalyse.log"
STRS="$ROOT/generated/strings_found.txt"
PAIRS="$ROOT/generated/strings_pairs.txt"
HITS="$ROOT/generated/strings_xrefs.txt"

echo "==> Procurando quais funcoes usam os textos achados no executavel"

if [[ ! -f "$LOG" ]]; then
    echo "(log da analise nao encontrado: $LOG)"
    exit 0
fi

grep '^\[strings\] .* 0x[0-9A-F]* "' "$LOG" | head -n 150 > "$STRS" || true

# Para cada endereco A calcula as duas formas de carregar o endereco no PPC:
#  lis hi / addi lo(com sinal)  e  lis hi / ori lo
: > "$PAIRS"

while IFS= read -r line; do
    addr_hex="$(printf '%s\n' "$line" | sed -E 's/^.* 0x([0-9A-F]+) ".*$/\1/')"
    addr=$((16#$addr_hex))

    lo=$((addr & 0xFFFF))
    slo=$lo
    if [[ "$lo" -ge 32768 ]]; then
        slo=$((lo - 65536))
    fi

    hi1=$(( ((addr + 0x8000) >> 16) & 0xFFFF ))
    l1=$((hi1 << 16))
    if [[ "$hi1" -ge 32768 ]]; then
        l1=$((l1 - 4294967296))
    fi

    hi2=$(( (addr >> 16) & 0xFFFF ))
    l2=$((hi2 << 16))
    if [[ "$hi2" -ge 32768 ]]; then
        l2=$((l2 - 4294967296))
    fi

    echo "$addr_hex $l1 $slo $l2 $lo" >> "$PAIRS"
done < "$STRS"

if [[ ! -s "$PAIRS" ]]; then
    echo "(nenhum texto encontrado; confira tools/search_strings.txt e o log da analise)"
    exit 0
fi

awk '
function flush(   i, a) {
    if (fn != "") {
        for (i = 1; i <= n; i++) {
            if ((hiSeen[l1[i]] && loSeen[slo[i]]) || (hiSeen[l2[i]] && uSeen[ulo[i]]))
                print addr[i], fn, curfile
        }
    }
    delete hiSeen; delete loSeen; delete uSeen
}
NR == FNR {
    n++; addr[n] = $1; l1[n] = $2; slo[n] = $3; l2[n] = $4; ulo[n] = $5
    hiRe = hiRe (hiRe == "" ? "" : "|") $2 "|" $4
    loRe = loRe (loRe == "" ? "" : "|") $3
    uRe = uRe (uRe == "" ? "" : "|") $5
    next
}
FNR == 1 && FILENAME != prevfile {
    if (prevfile != "") flush()
    prevfile = FILENAME
    curfile = FILENAME; sub(".*/", "", curfile)
    fn = ""
}
/^PPC_FUNC_IMPL\(__imp__sub_/ {
    flush()
    fn = $0
    sub(/^PPC_FUNC_IMPL\(__imp__/, "", fn)
    sub(/\).*/, "", fn)
}
fn != "" {
    if ($0 ~ ("= (" hiRe ");")) {
        t = $0; sub(/^[^=]*= /, "", t); sub(/;.*/, "", t); hiSeen[t] = 1
    }
    if ($0 ~ ("\\+ (" loRe ")[;,)]")) {
        t = $0; sub(/^.*\+ /, "", t); sub(/[;,)].*/, "", t); loSeen[t] = 1
    }
    if ($0 ~ ("\\| (" uRe ");")) {
        t = $0; sub(/^.*\| /, "", t); sub(/;.*/, "", t); uSeen[t] = 1
    }
}
END { flush() }
' "$PAIRS" "$OUT_DIR"/ppc_recomp.*.cpp | sort -u > "$HITS" || true

echo
echo "==== INICIO STRINGS ===="

while IFS= read -r line; do
    addr_hex="$(printf '%s\n' "$line" | sed -E 's/^.* 0x([0-9A-F]+) ".*$/\1/')"
    text="$(printf '%s\n' "$line" | sed -E 's/^[^"]*"(.*)"$/\1/' | cut -c1-90)"
    count="$(awk -v a="$addr_hex" '$1 == a' "$HITS" | wc -l)"
    echo "0x$addr_hex \"$text\" -> $count funcao(oes)"
    awk -v a="$addr_hex" '$1 == a { print "    " $2 " (" $3 ")" }' "$HITS" | head -n 6 || true
done < "$STRS"

echo "==== FIM STRINGS ===="
echo

{
    echo "# ==== FILE: strings_found.txt ===="
    head -n 300 "$STRS" | sed 's/^/# /'
    echo "# ==== END FILE ===="
    echo "# ==== FILE: strings_xrefs.txt ===="
    head -n 3000 "$HITS" | sed 's/^/# /'
    echo "# ==== END FILE ===="
} >> "$TOML_OUT"
