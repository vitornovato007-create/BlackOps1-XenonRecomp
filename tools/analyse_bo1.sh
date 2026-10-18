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

echo "==> Instalando a sonda de diagnóstico no XenonAnalyse"

MAIN_CPP="$ROOT/XenonRecomp/XenonAnalyse/main.cpp"

if [[ ! -f "$ROOT/tools/bo1_probe.h" ]]; then
    echo "::error::tools/bo1_probe.h não encontrado."
    exit 7
fi

cp "$ROOT/tools/bo1_probe.h" "$ROOT/XenonRecomp/XenonAnalyse/bo1_probe.h"

if ! grep -q 'bo1_probe.h' "$MAIN_CPP"; then
    sed -i 's|#include "function.h"|#include "function.h"\n#include "bo1_probe.h"|' "$MAIN_CPP"
    sed -i 's|^\(\s*\)auto image = Image::ParseImage(file.data(), file.size());|&\n\1BO1Probe(image);|' "$MAIN_CPP"
fi

if ! grep -q 'bo1_probe.h' "$MAIN_CPP" || ! grep -q 'BO1Probe(image);' "$MAIN_CPP"; then
    echo "::error::Não consegui inserir a sonda no main.cpp do XenonAnalyse."
    echo "O arquivo oficial pode ter mudado. Trecho relevante:"
    grep -n 'include\|ParseImage' "$MAIN_CPP" || true
    exit 7
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

echo "==> Diagnóstico do XEX"
echo "Arquivo: $XEX"
echo "Tamanho em bytes: $(stat -c %s "$XEX")"
echo "Primeiros 16 bytes (hex):"
od -An -tx1 -N16 "$XEX"
echo "(um XEX2 válido começa com: 58 45 58 32)"

mkdir -p "$(dirname "$OUTPUT")"
LOG="$ROOT/generated/xenonanalyse.log"

echo "==> Analisando o executável BO1"

"$ANALYSER" "$XEX" "$OUTPUT" 2>&1 | tee "$LOG"

echo
echo "==== INICIO PROBE ===="
grep '^\[probe\]' "$LOG" || echo "(nenhuma linha de probe)"
echo "==== FIM PROBE ===="
echo

if [[ ! -s "$OUTPUT" ]]; then
    echo "::error::O analisador não produziu um arquivo de saída."
    exit 5
fi

BLOCKS="$(grep -c '^\[\[' "$OUTPUT" || true)"

echo "==> Resultado da análise"
echo "Arquivo gerado: $OUTPUT"
ls -lh "$OUTPUT"
echo "Linhas no arquivo: $(wc -l < "$OUTPUT")"
echo "Blocos de jump table encontrados: $BLOCKS"

if [[ "$BLOCKS" -eq 0 ]]; then
    echo "::error::O TOML não contém nenhuma jump table. A análise não achou nada."
    echo "Veja o bloco PROBE acima para entender os padrões do BO1."
    exit 6
fi

echo "==> Análise concluída"
echo "A saída ainda precisa ser validada para o Black Ops 1."
