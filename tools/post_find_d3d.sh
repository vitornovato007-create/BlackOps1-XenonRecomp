#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="$ROOT/generated/ppc"
TOML_OUT="$ROOT/generated/BO1_switch_tables.toml"
RESULT="$ROOT/generated/d3d_callers.txt"

echo "==> Procurando quem chama as importações de vídeo (Vd*) no C++ gerado"

awk '
FNR == 1 { curfile = FILENAME; sub(".*/", "", curfile) }
/^PPC_FUNC_IMPL\(__imp__sub_/ {
    fn = $0
    sub(/^PPC_FUNC_IMPL\(__imp__/, "", fn)
    sub(/\).*/, "", fn)
}
/__imp__Vd[A-Za-z0-9_]*\(ctx, base\)/ {
    imp = $0
    sub(/^.*__imp__/, "", imp)
    sub(/\(ctx, base\).*/, "", imp)
    key = imp " " fn " " curfile
    if (!(key in seen)) { seen[key] = 1; print imp, fn, curfile }
}
' "$OUT_DIR"/ppc_recomp.*.cpp | sort > "$RESULT" || true

echo
echo "==== INICIO D3D ===="
echo "Importacoes Vd* chamadas por funcoes do jogo: $(awk '{print $1}' "$RESULT" | sort -u | wc -l)"
echo
echo "-- chamadores por importacao (ate 8 cada) --"

for imp in $(awk '{print $1}' "$RESULT" | sort -u); do
    total="$(awk -v i="$imp" '$1 == i' "$RESULT" | wc -l)"
    echo "$imp ($total):"
    awk -v i="$imp" '$1 == i { print "    " $2 " (" $3 ")" }' "$RESULT" | head -n 8 || true
done

echo
echo "-- faixa de enderecos das funcoes que chamam Vd* (palpite da regiao do D3D9) --"
awk '{ a = $2; sub(/^sub_/, "", a); print a }' "$RESULT" | sort -u | sed -n '1p;$p' || true
echo "==== FIM D3D ===="
echo

{
    echo "# ==== FILE: d3d_callers.txt ===="
    head -n 500 "$RESULT" | sed 's/^/# /'
    echo "# ==== END FILE ===="
} >> "$TOML_OUT"
