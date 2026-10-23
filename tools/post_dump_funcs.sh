#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="$ROOT/generated/ppc"
TOML_OUT="$ROOT/generated/BO1_switch_tables.toml"
LIST="$ROOT/tools/dump_functions.txt"
MAX_LINES=400

if [[ ! -f "$LIST" ]]; then
    echo "(tools/dump_functions.txt não encontrado, nada a extrair)"
    exit 0
fi

echo "==> Extraindo o C++ gerado das funções listadas em tools/dump_functions.txt"

while read -r name _; do
    if [[ -z "$name" || "$name" == \#* ]]; then
        continue
    fi

    file="$(grep -l -F "PPC_FUNC_IMPL(__imp__${name})" "$OUT_DIR"/ppc_recomp.*.cpp 2>/dev/null | head -n 1 || true)"

    if [[ -z "$file" ]]; then
        echo "$name: não encontrada"
        {
            echo "# ==== FILE: func_${name}.cpp ===="
            echo "# (funcao nao encontrada)"
            echo "# ==== END FILE ===="
        } >> "$TOML_OUT"
        continue
    fi

    echo "$name: $(basename "$file")"

    {
        echo "# ==== FILE: func_${name}.cpp ===="
        echo "# arquivo: $(basename "$file")"
        awk -v fn="PPC_FUNC_IMPL(__imp__${name})" -v max="$MAX_LINES" '
            index($0, fn) == 1 { on = 1 }
            on {
                n++
                if (n <= max) print
                if ($0 == "}") { if (n > max) print "... (truncado em " max " linhas, total " n ")"; exit }
            }
        ' "$file" | sed 's/^/# /'
        echo "# ==== END FILE ===="
    } >> "$TOML_OUT"
done < "$LIST"
