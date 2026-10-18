#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

XEX="${1:-$ROOT/game/default.xex}"
OUTPUT="${2:-$ROOT/generated/BO1_switch_tables.toml}"

if [[ "$XEX" != /* ]]; then
    XEX="$ROOT/$XEX"
fi

if [[ ! -f "$XEX" ]]; then
    echo "::error::Executável BO1 não encontrado: $XEX"
    exit 2
fi

mkdir -p "$ROOT/generated" "$ROOT/build"

echo "==> Preparando XenonRecomp oficial"

if [[ -d "$ROOT/XenonRecomp/.git" ]]; then
    git -C "$ROOT/XenonRecomp" submodule update --init --recursive
elif [[ -e "$ROOT/XenonRecomp" ]]; then
    echo "::error::XenonRecomp existe, mas não é um checkout Git válido."
    exit 3
else
    git clone --recurse-submodules \
        https://github.com/hedge-dev/XenonRecomp.git \
        "$ROOT/XenonRecomp"
fi

echo "==> Configurando XenonRecomp"

cmake \
    -S "$ROOT/XenonRecomp" \
    -B "$ROOT/build/xenonrecomp" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++

echo "==> Compilando ferramentas oficiais"

cmake --build "$ROOT/build/xenonrecomp" --parallel 2

echo "==> Localizando XenonAnalyse"

ANALYSER="$ROOT/build/xenonrecomp/XenonAnalyse/XenonAnalyse"

if [[ ! -f "$ANALYSER" || ! -x "$ANALYSER" ]]; then
    ANALYSER="$(
        find "$ROOT/build/xenonrecomp" \
            -type f \
            -name XenonAnalyse \
            -perm -111 \
            -print -quit
    )"
fi

if [[ -z "$ANALYSER" || ! -f "$ANALYSER" || ! -x "$ANALYSER" ]]; then
    echo "::error::Executável XenonAnalyse não encontrado."
    echo "Conteúdo da pasta de compilação:"
    find "$ROOT/build/xenonrecomp" -maxdepth 4 -name '*XenonAnalyse*' -print
    exit 4
fi

echo "Analisador encontrado: $ANALYSER"

mkdir -p "$(dirname "$OUTPUT")"

echo "==> Analisando o executável BO1"

"$ANALYSER" "$XEX" "$OUTPUT"

if [[ ! -s "$OUTPUT" ]]; then
    echo "::error::O analisador não produziu um arquivo de saída válido."
    echo "Verifique a mensagem de uso do XenonAnalyse e o formato de entrada."
    exit 5
fi

echo "==> Análise concluída"
echo "Arquivo gerado: $OUTPUT"
ls -lh "$OUTPUT"
echo "A saída ainda precisa ser validada para o Black Ops 1."
