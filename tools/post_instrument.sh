#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="$ROOT/generated/ppc"
PATCH_PY="$ROOT/tools/patch_instrument.py"
REPORT="$ROOT/generated/instrument_report.txt"

echo "==> post_instrument.sh: injetando hooks de log nos .cpp gerados"

# -----------------------------------------------------------------------------
# 1. Sanidade: o diretorio gerado existe e tem arquivos?
# -----------------------------------------------------------------------------

if [[ ! -d "$OUT_DIR" ]]; then
    echo "::error::generated/ppc nao existe. O XenonRecomp rodou?"
    exit 1
fi

FILES_COUNT="$(find "$OUT_DIR" -type f -name '*.cpp' | wc -l)"
if [[ "$FILES_COUNT" -eq 0 ]]; then
    echo "::error::generated/ppc nao tem nenhum .cpp. O XenonRecomp gerou algo?"
    exit 1
fi

echo "    Arquivos .cpp encontrados: $FILES_COUNT"

# -----------------------------------------------------------------------------
# 2. Sanidade: o script Python existe?
# -----------------------------------------------------------------------------

if [[ ! -f "$PATCH_PY" ]]; then
    echo "::error::tools/patch_instrument.py nao encontrado."
    exit 1
fi

# -----------------------------------------------------------------------------
# 3. Roda o patch
# -----------------------------------------------------------------------------

rm -f "$REPORT"

set +e
python3 "$PATCH_PY" "$OUT_DIR" "$REPORT"
STATUS=$?
set -e

if [[ "$STATUS" -ne 0 ]]; then
    echo "::error::patch_instrument.py falhou (codigo $STATUS)."
    echo "---- ultimas linhas do relatorio ----"
    [[ -f "$REPORT" ]] && tail -n 50 "$REPORT" || true
    echo "-------------------------------------"
    exit 1
fi

if [[ ! -f "$REPORT" ]]; then
    echo "::error::patch_instrument.py nao gerou o relatorio esperado em $REPORT."
    exit 1
fi

# -----------------------------------------------------------------------------
# 4. Verifica se TODAS as 4 funcoes alvo foram patchadas
#
# O patch_instrument.py escreve, para cada funcao alvo, uma linha:
#   PATCHED: sub_824D1EE8
#   PATCHED: sub_825FC8F0
#   PATCHED: sub_82604428
#   PATCHED: sub_82603680
# Se alguma nao foi encontrada, escreve:
#   MISSING: sub_XXXX
# -----------------------------------------------------------------------------

TARGETS=("sub_824D1EE8" "sub_825FC8F0" "sub_82604428" "sub_82603680")
MISSING=()

for t in "${TARGETS[@]}"; do
    if ! grep -q "^PATCHED: $t$" "$REPORT"; then
        MISSING+=("$t")
    fi
done

if [[ "${#MISSING[@]}" -ne 0 ]]; then
    echo "::error::As seguintes funcoes NAO foram patchadas: ${MISSING[*]}"
    echo "---- relatorio completo ----"
    cat "$REPORT"
    echo "----------------------------"
    exit 1
fi

# -----------------------------------------------------------------------------
# 5. Sucesso
# -----------------------------------------------------------------------------

echo "    Hooks aplicados com sucesso em: ${TARGETS[*]}"
echo "    Relatorio: $REPORT"

# Mostra um resumo (linhas PATCHED / linhas de hook injetadas)
HOOKS_TOTAL="$(grep -c '^HOOK:' "$REPORT" || true)"
echo "    Total de hooks injetados: $HOOKS_TOTAL"

exit 0
