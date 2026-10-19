#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CONFIG="$ROOT/config/BO1.toml"
OUT_DIR="$ROOT/generated/ppc"
LOG="$ROOT/generated/xenonrecomp.log"
TOML_OUT="$ROOT/generated/BO1_switch_tables.toml"

echo "==> Etapa 2: XenonRecomp (gerar C++ do BO1)"

if [[ ! -f "$CONFIG" ]]; then
    echo "::error::config/BO1.toml não encontrado."
    exit 8
fi

PPC_CONTEXT="$ROOT/XenonRecomp/XenonUtils/ppc_context.h"

if [[ ! -f "$PPC_CONTEXT" ]]; then
    PPC_CONTEXT="$(find "$ROOT/XenonRecomp" -name ppc_context.h -print -quit)"
fi

if [[ -z "$PPC_CONTEXT" || ! -f "$PPC_CONTEXT" ]]; then
    echo "::error::ppc_context.h não encontrado no XenonRecomp."
    exit 8
fi

RECOMP="$ROOT/build/xenonrecomp/XenonRecomp/XenonRecomp"

if [[ ! -f "$RECOMP" || ! -x "$RECOMP" ]]; then
    RECOMP="$(
        find "$ROOT/build/xenonrecomp" \
            -type f \
            -name XenonRecomp \
            -perm -111 \
            -print -quit
    )"
fi

if [[ -z "$RECOMP" || ! -f "$RECOMP" || ! -x "$RECOMP" ]]; then
    echo "::error::Executável XenonRecomp não encontrado."
    exit 8
fi

echo "Recompilador: $RECOMP"
echo "Configuração: $CONFIG"
echo "ppc_context.h: $PPC_CONTEXT"

mkdir -p "$OUT_DIR"
find "$OUT_DIR" -mindepth 1 -delete

set +e
"$RECOMP" "$CONFIG" "$PPC_CONTEXT" > "$LOG" 2>&1
STATUS=$?
set -e

FILES="$(find "$OUT_DIR" -type f | wc -l)"

echo
echo "==== INICIO RECOMP ===="
echo "Código de saída do XenonRecomp: $STATUS"
echo "Linhas no log: $(wc -l < "$LOG")"
echo "Arquivos gerados em generated/ppc: $FILES"
echo "Tamanho total: $(du -sh "$OUT_DIR" | cut -f1)"
echo
echo "-- mensagens mais frequentes (endereços e números normalizados) --"
grep -i -E 'warn|unrecogn|unimpl|invalid|error|fail|unable' "$LOG" \
    | sed -E 's/0x[0-9A-Fa-f]+/0xADDR/g; s/[0-9]+/N/g' \
    | sort | uniq -c | sort -rn | head -n 25 || true
echo "==== FIM RECOMP ===="
echo

echo "==> Anexando material de diagnóstico ao fim do TOML (como comentários)"

append_file() {
    local title="$1"
    local path="$2"
    local maxlines="$3"

    {
        echo "# ==== FILE: $title ===="

        if [[ -f "$path" ]]; then
            head -n "$maxlines" "$path" | sed 's/^/# /'
        else
            echo "# (nao encontrado: $path)"
        fi

        echo "# ==== END FILE ===="
    } >> "$TOML_OUT"
}

SRC="$ROOT/XenonRecomp"

append_file "xenonrecomp.log" "$LOG" 6000
append_file "XenonRecomp/recompiler.cpp" "$SRC/XenonRecomp/recompiler.cpp" 20000
append_file "XenonRecomp/recompiler.h" "$SRC/XenonRecomp/recompiler.h" 2000
append_file "XenonRecomp/recompiler_config.cpp" "$SRC/XenonRecomp/recompiler_config.cpp" 1500
append_file "XenonRecomp/recompiler_config.h" "$SRC/XenonRecomp/recompiler_config.h" 500
append_file "XenonUtils/ppc_context.h" "$SRC/XenonUtils/ppc_context.h" 3000
append_file "XenonUtils/image.h" "$SRC/XenonUtils/image.h" 500
append_file "XenonAnalyse/function.h" "$SRC/XenonAnalyse/function.h" 500
append_file "XenonAnalyse/function.cpp" "$SRC/XenonAnalyse/function.cpp" 2000

SAMPLE="$(find "$OUT_DIR" -name 'ppc_recomp.0.cpp' -print -quit)"
append_file "generated/ppc_recomp.0.cpp (inicio)" "${SAMPLE:-/nao/existe}" 150

CONFIG_H="$(find "$OUT_DIR" -name 'ppc_config.h' -print -quit)"
append_file "generated/ppc_config.h" "${CONFIG_H:-/nao/existe}" 200

{
    echo "# ==== FILE: lista de arquivos gerados ===="
    find "$OUT_DIR" -type f -printf '%s %p\n' | sort -k2 | sed 's/^/# /'
    echo "# ==== END FILE ===="
} >> "$TOML_OUT"

echo "Tamanho do TOML com anexos: $(stat -c %s "$TOML_OUT") bytes"

if [[ "$STATUS" -ne 0 ]]; then
    echo "::error::O XenonRecomp terminou com erro (código $STATUS). Veja o bloco RECOMP acima."
    exit 9
fi

if [[ "$FILES" -eq 0 ]]; then
    echo "::error::O XenonRecomp não gerou nenhum arquivo."
    exit 10
fi

echo "==> XenonRecomp terminou e gerou código C++."
echo "ATENÇÃO: isso não prova que o código compila nem que o jogo funciona."
