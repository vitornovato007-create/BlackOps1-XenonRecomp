#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="$ROOT/generated/ppc"
TOML_OUT="$ROOT/generated/BO1_switch_tables.toml"
LIST="$ROOT/tools/search_code.txt"
RESULT="$ROOT/generated/code_search.txt"

if [[ ! -f "$LIST" ]]; then
    echo "(tools/search_code.txt não encontrado, nada a procurar)"
    exit 0
fi

echo "==> Procurando os padrões de tools/search_code.txt no C++ gerado"

# Formato de cada linha: texto [@INICIO-FIM]   (enderecos de 8 digitos hex, opcional)
awk '
NR == FNR {
    line = $0
    if (line ~ /^[ \t]*#/ || line ~ /^[ \t]*$/) next
    lo = ""; hi = ""
    pos = index(line, " @")
    if (pos > 0) {
        range = substr(line, pos + 2)
        line = substr(line, 1, pos - 1)
        split(range, r, "-")
        lo = toupper(r[1]); hi = toupper(r[2])
    }
    n++; pat[n] = line; plo[n] = lo; phi[n] = hi
    next
}
FNR == 1 { curfile = FILENAME; sub(".*/", "", curfile) }
/^PPC_FUNC_IMPL\(__imp__sub_/ {
    fn = $0
    sub(/^PPC_FUNC_IMPL\(__imp__/, "", fn)
    sub(/\).*/, "", fn)
    fa = fn
    sub(/^sub_/, "", fa)
}
fn != "" {
    for (i = 1; i <= n; i++) {
        if (index($0, pat[i]) > 0) {
            if (plo[i] != "" && (fa < plo[i] || fa > phi[i])) continue
            cnt[i " " fn " " curfile]++
        }
    }
}
END { for (k in cnt) print k, cnt[k] }
' "$LIST" "$OUT_DIR"/ppc_recomp.*.cpp | sort -k1,1n -k2,2 > "$RESULT" || true

echo
echo "==== INICIO GREP ===="

idx=0

while IFS= read -r line; do
    if [[ "$line" =~ ^[[:space:]]*# || "$line" =~ ^[[:space:]]*$ ]]; then
        continue
    fi

    idx=$((idx + 1))
    total="$(awk -v i="$idx" '$1 == i' "$RESULT" | wc -l)"
    echo "[$idx] $line -> $total funcao(oes)"
    awk -v i="$idx" '$1 == i { print "    " $2 " (" $3 ") x" $4 }' "$RESULT" | head -n 15 || true
done < "$LIST"

echo "==== FIM GREP ===="
echo

{
    echo "# ==== FILE: code_search.txt ===="
    head -n 3000 "$RESULT" | sed 's/^/# /'
    echo "# ==== END FILE ===="
} >> "$TOML_OUT"
