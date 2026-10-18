#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

XEX="${1:-$ROOT/game/default.xex}"
OUTPUT="${2:-$ROOT/generated/BO1_switch_tables.toml}"

if [[ ! -f "$XEX" ]]; then
    echo "::error::Executável BO1 não encontrado: $XEX"
    exit 2
fi

mkdir -p generated build

if [[ ! -d XenonRecomp/.git ]]; then
    rm -rf XenonRecomp
    git clone --recursive \
        https://github.com/hedge-dev/XenonRecomp.git \
        XenonRecomp
else
    git -C XenonRecomp submodule update --init --recursive
fi

cmake -S XenonRecomp -B build/xenonrecomp \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++

cmake --build build/xenonrecomp --parallel 2

ANALYSER=""

for candidate in \
    build/xenonrecomp/XenonAnalyse \
    build/xenonrecomp/XenonAnalyse/XenonAnalyse
do
    if [[ -x "$candidate" ]]; then
        ANALYSER="$candidate"
        break
    fi
done

if [[ -z "$ANALYSER" ]]; then
    ANALYSER="$(find build/xenonrecomp -type f \
        -name XenonAnalyse -perm -111 -print -quit)"
fi

if [[ -z "$ANALYSER" ]]; then
    echo "::error::A compilação terminou, mas XenonAnalyse não foi localizado."
    echo "Confira os alvos e os caminhos de saída do CMake upstream."
    exit 4
fi

mkdir -p "$(dirname "$OUTPUT")"

"$ANALYSER" "$XEX" "$OUTPUT"

if [[ ! -s "$OUTPUT" ]]; then
    echo "::error::O analisador não produziu a tabela de saltos."
    exit 5
fi

echo "Análise concluída: $OUTPUT"
echo "A tabela gerada precisa ser revisada antes da recompilação completa."
