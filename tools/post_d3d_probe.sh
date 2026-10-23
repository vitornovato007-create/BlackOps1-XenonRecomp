#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="$ROOT/generated/ppc"
TOML_OUT="$ROOT/generated/BO1_switch_tables.toml"
OFFS="$ROOT/generated/d3d_offsets.txt"
GRAPH="$ROOT/generated/d3d_graph.txt"

# Janela de enderecos em volta do D3D9 do BO1 (8 digitos hex, maiusculos)
WIN_LO="82480000"
WIN_HI="824E0000"

echo "==> Sonda do D3D9: deslocamentos do dispositivo e grafo de chamadas"

# Parte A: deslocamentos que o D3D9 do MW2 usa no dispositivo
# (10548 estado de render, 10896 progresso da arena, 13520/13524/13528 buffer de comandos)
awk '
FNR == 1 { curfile = FILENAME; sub(".*/", "", curfile) }
/^PPC_FUNC_IMPL\(__imp__sub_/ {
    fn = $0
    sub(/^PPC_FUNC_IMPL\(__imp__/, "", fn)
    sub(/\).*/, "", fn)
}
/\+ (10548|10896|13520|13524|13528)[,)]/ {
    off = $0
    sub(/^.*\+ /, "", off)
    sub(/[,)].*/, "", off)
    cnt[off " " fn " " curfile]++
}
END { for (k in cnt) print k, cnt[k] }
' "$OUT_DIR"/ppc_recomp.*.cpp | sort -k1,1 -k4,4nr > "$OFFS" || true

# Parte B: grafo de chamadas das funcoes em volta do D3D9
awk -v lo="$WIN_LO" -v hi="$WIN_HI" '
FNR == 1 { curfile = FILENAME; sub(".*/", "", curfile) }
/^PPC_FUNC_IMPL\(__imp__sub_/ {
    if (fn != "" && fa >= lo && fa <= hi) print "F", fn, lines
    fn = $0
    sub(/^PPC_FUNC_IMPL\(__imp__/, "", fn)
    sub(/\).*/, "", fn)
    fa = fn
    sub(/^sub_/, "", fa)
    lines = 0
}
{ lines++ }
/^\tsub_[0-9A-F]+\(ctx, base\);/ {
    callee = $0
    sub(/^\t/, "", callee)
    sub(/\(ctx, base\).*/, "", callee)
    ca = callee
    sub(/^sub_/, "", ca)
    if ((fa >= lo && fa <= hi) || (ca >= lo && ca <= hi)) print "E", fn, callee
}
END { if (fn != "" && fa >= lo && fa <= hi) print "F", fn, lines }
' "$OUT_DIR"/ppc_recomp.*.cpp | sort -u > "$GRAPH" || true

echo
echo "==== INICIO D3DPROBE ===="
echo "Funcoes na janela $WIN_LO..$WIN_HI: $(grep -c '^F ' "$GRAPH" || true)"
echo "Arestas de chamada que tocam a janela: $(grep -c '^E ' "$GRAPH" || true)"
echo
for off in 10548 10896 13520 13524 13528; do
    echo "-- deslocamento +$off: funcoes que mais o usam --"
    awk -v o="$off" '$1 == o { print "    " $2 " (" $3 ") x" $4 }' "$OFFS" | head -n 8 || true
done
echo "==== FIM D3DPROBE ===="
echo

{
    echo "# ==== FILE: d3d_offsets.txt ===="
    head -n 2000 "$OFFS" | sed 's/^/# /'
    echo "# ==== END FILE ===="
    echo "# ==== FILE: d3d_graph.txt ===="
    head -n 40000 "$GRAPH" | sed 's/^/# /'
    echo "# ==== END FILE ===="
} >> "$TOML_OUT"
