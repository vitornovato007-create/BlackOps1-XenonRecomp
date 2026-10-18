
#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

XEX="${BO1_XEX:-${1:-$ROOT/game/default.xex}}"

if [[ "$XEX" != /* ]]; then
    XEX="$ROOT/$XEX"
fi

if [[ ! -f "$XEX" ]]; then
    echo "::error::Executável BO1 não encontrado: $XEX"
    echo "Coloque o default.xex em game/default.xex."
    exit 2
fi

for tool in cmake ninja git clang clang++; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "::error::Ferramenta necessária não encontrada: $tool"
        exit 3
    fi
done

echo "=========================================="
echo " BlackOps1-XenonRecomp"
echo " Etapa: análise real do executável"
echo "=========================================="

cmake \
    -S "$ROOT" \
    -B "$ROOT/build" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBO1_XEX="$XEX"

cmake --build "$ROOT/build" \
    --target bo1_analyse \
    --parallel 2

OUTPUT="$ROOT/generated/BO1_switch_tables.toml"

if [[ ! -s "$OUTPUT" ]]; then
    echo "::error::A tabela de análise não foi gerada."
    exit 4
fi

echo
echo "SUCESSO: o estágio de análise terminou."
echo "Tabela: $OUTPUT"
echo
echo "ATENÇÃO: a recompilação completa do BO1 ainda não foi executada."
echo "A próxima etapa exige uma configuração BO1 validada."
