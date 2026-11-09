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

echo "==> Anexando o log do XenonRecomp ao fim do TOML (como comentários)"

{
    echo "# ==== FILE: xenonrecomp.log ===="
    head -n 6000 "$LOG" | sed 's/^/# /'
    echo "# ==== END FILE ===="
} >> "$TOML_OUT"

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

bash "$ROOT/tools/check_generated.sh" || true

# -----------------------------------------------------------------------------
# Passos extras: qualquer tools/post_*.sh roda aqui, em ordem alfabetica.
#
# IMPORTANTE:
#   - tools/post_instrument.sh e OBRIGATORIO. Se ele falhar, o build FALHA.
#     Isso garante que o APK final SEMPRE tenha a instrumentacao de log
#     (hooks em sub_824D1EE8, sub_825FC8F0, sub_82604428, sub_82603680)
#     injetada nos .cpp gerados. Se o patch nao aplicar, e melhor descobrir
#     agora do que gerar um APK sem diagnostico e perder tempo de teste.
#   - Os demais tools/post_*.sh continuam opcionais (|| true).
# -----------------------------------------------------------------------------

for post in "$ROOT"/tools/post_*.sh; do
    if [[ -f "$post" ]]; then
        echo "==> Executando $(basename "$post")"

        if [[ "$(basename "$post")" == "post_instrument.sh" ]]; then
            # Obrigatorio: falha = build falha
            if ! bash "$post"; then
                echo "::error::$(basename "$post") FALHOU — o APK sairia sem instrumentacao. Abortando o build."
                exit 11
            fi
        else
            # Opcional
            bash "$post" || true
        fi
    fi
done

echo "==> Pipeline do XenonRecomp concluido."
