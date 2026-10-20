#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="$ROOT/generated/ppc"
TOML_OUT="$ROOT/generated/BO1_switch_tables.toml"
CHECK_LOG="$ROOT/generated/compile_check.log"

echo "==> Etapa 3: checagem de sintaxe do C++ gerado (clang++ -fsyntax-only)"

if ! command -v clang++ >/dev/null 2>&1; then
    echo "::warning::clang++ não encontrado, pulando a checagem."
    exit 0
fi

SIMDE_HEADER="$(find "$ROOT/XenonRecomp/thirdparty/simde" -path '*x86/avx.h' -print -quit)"

if [[ -z "$SIMDE_HEADER" ]]; then
    echo "::warning::Cabeçalhos do simde não encontrados, pulando a checagem."
    exit 0
fi

SIMDE_DIR="$(dirname "$(dirname "$SIMDE_HEADER")")"
echo "simde: $SIMDE_DIR"

pick() {
    local pattern="$1"
    grep -l -F -- "$pattern" "$OUT_DIR"/ppc_recomp.*.cpp 2>/dev/null | head -n 1 || true
}

CANDIDATES=("$OUT_DIR/ppc_recomp.0.cpp")

for pattern in 'loc_82566F5C:' '// vsel128' '// stfsu ' '// lhzu ' '// bdzf'; do
    found="$(pick "$pattern")"

    if [[ -n "$found" ]]; then
        CANDIDATES+=("$found")
    fi
done

CANDIDATES+=("$OUT_DIR/ppc_func_mapping.cpp")

mapfile -t FILES < <(printf '%s\n' "${CANDIDATES[@]}" | sort -u)

: > "$CHECK_LOG"

for file in "${FILES[@]}"; do
    name="$(basename "$file")"
    echo "---- $name ----" >> "$CHECK_LOG"

    start="$(date +%s)"

    set +e
    timeout 400 clang++ -std=c++20 -fsyntax-only -w -ferror-limit=40 \
        -I"$OUT_DIR" -I"$SIMDE_DIR" "$file" >> "$CHECK_LOG" 2>&1
    status=$?
    set -e

    end="$(date +%s)"

    echo "RESULT $name saida=$status tempo=$((end - start))s" >> "$CHECK_LOG"
    echo "$name: saida=$status tempo=$((end - start))s"
done

TOTAL_ERRORS="$(grep -c 'error:' "$CHECK_LOG" || true)"

echo
echo "==== INICIO COMPILE ===="
echo "Arquivos checados: ${#FILES[@]}"
echo "Total de linhas com error: $TOTAL_ERRORS"
echo
echo "-- resultado por arquivo --"
grep '^RESULT' "$CHECK_LOG" || true
echo
echo "-- erros mais frequentes (números normalizados) --"
grep 'error:' "$CHECK_LOG" \
    | sed -E 's#^[^ ]*: ##; s/0x[0-9A-Fa-f]+/0xN/g; s/[0-9]+/N/g' \
    | cut -c1-200 \
    | sort | uniq -c | sort -rn | head -n 25 || true
echo
echo "-- primeiros 25 erros --"
grep 'error:' "$CHECK_LOG" \
    | sed -E 's#^.*/generated/ppc/##' \
    | cut -c1-220 | head -n 25 || true
echo "==== FIM COMPILE ===="
echo

{
    echo "# ==== FILE: compile_check.log ===="
    head -n 4000 "$CHECK_LOG" | sed 's/^/# /'
    echo "# ==== END FILE ===="
} >> "$TOML_OUT"

echo "==> Checagem de sintaxe terminou (isto não gera binário nem prova que o jogo roda)."
