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
    echo "::error::A pasta XenonRecomp existe, mas não é um checkout Git válido."
    echo "Verifique a pasta antes de tentar novamente."
    exit 3
else
    git clone --recurse-submodules \
        https://github.com/hedge-dev/XenonRecomp.git \
        "$ROOT/XenonRecomp"
fi

echo "==> Configurando as ferramentas oficiais"

cmake \
    -S "$ROOT/XenonRecomp" \
    -B "$ROOT/build/xenonrecomp" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++

echo "==> Compilando XenonAnalyse e XenonRecomp"

cmake --build "$ROOT/build/xenonrecomp" --parallel 2

ANALYSER="$ROOT/build/xenonrecomp/XenonAnalyse"

if [[ ! -x "$ANALYSER" ]]; then
    ANALYSER="$(
        find "$ROOT/build/xenonrecomp" \
            -type f \
            -name XenonAnalyse \
            -perm -111 \
            -print -quit
    )"
fi

if [[ -z "$ANALYSER" || ! -x "$ANALYSER" ]]; then
    echo "::error::XenonAnalyse não foi encontrado após a compilação."
    exit 4
fi

mkdir -p "$(dirname "$OUTPUT")"

echo "==> Analisando o executável do BO1"

"$ANALYSER" "$XEX" "$OUTPUT"

if [[ ! -s "$OUTPUT" ]]; then
    echo "::error::A análise não produziu um arquivo TOML não vazio."
    echo "Pode ser necessário adaptar o analisador ao formato do BO1."
    exit 5
fi

echo "==> Análise concluída"
echo "Arquivo gerado: $OUTPUT"
echo "A tabela ainda precisa ser validada para o BO1."
