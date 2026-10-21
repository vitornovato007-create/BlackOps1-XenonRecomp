#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="$ROOT/generated/ppc"
TOML_OUT="$ROOT/generated/BO1_switch_tables.toml"
CHECK_LOG="$ROOT/generated/compile_check.log"

echo "==> Etapa 3: checagens do C++ gerado"

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

# Arquivos prioritarios: os que contem o que mais mexemos
PRIORITY=("$OUT_DIR/ppc_recomp.0.cpp")

for pattern in 'loc_82566F5C:' '// vsel128' '// stfsu ' '// lhzu ' '// bdzf'; do
    found="$(pick "$pattern")"

    if [[ -n "$found" ]]; then
        PRIORITY+=("$found")
    fi
done

PRIORITY+=("$OUT_DIR/ppc_func_mapping.cpp")

mapfile -t PRIORITY_FILES < <(printf '%s\n' "${PRIORITY[@]}" | awk '!seen[$0]++')

# Todos os arquivos, com os prioritarios primeiro
mapfile -t ALL_FILES < <( { printf '%s\n' "${PRIORITY_FILES[@]}"; find "$OUT_DIR" -name '*.cpp' | sort; } | awk '!seen[$0]++')

: > "$CHECK_LOG"

check_batch() {
    # $1 = rotulo, $2 = compilador, $3 = limite de tempo (s), demais = arquivos
    local label="$1"
    local compiler="$2"
    local budget="$3"
    shift 3

    local ok=0
    local failed=0
    local skipped=0
    local started="$SECONDS"
    local file
    local name
    local status

    for file in "$@"; do
        if (( SECONDS - started > budget )); then
            skipped=$((skipped + 1))
            continue
        fi

        name="$(basename "$file")"
        echo "---- [$label] $name ----" >> "$CHECK_LOG"

        set +e
        timeout 300 "$compiler" -std=c++20 -fsyntax-only -w -ferror-limit=20 \
            -I"$OUT_DIR" -I"$SIMDE_DIR" "$file" >> "$CHECK_LOG" 2>&1
        status=$?
        set -e

        if [[ "$status" -eq 0 ]]; then
            ok=$((ok + 1))
        else
            failed=$((failed + 1))
            echo "RESULT [$label] $name saida=$status" >> "$CHECK_LOG"
        fi
    done

    echo "RESUMO [$label] ok=$ok falhou=$failed nao_testados_por_tempo=$skipped tempo=$((SECONDS - started))s" | tee -a "$CHECK_LOG"
}

echo "==> x86_64: sintaxe de todos os arquivos (limite de 600 s)"
check_batch "x86_64" clang++ 600 "${ALL_FILES[@]}"

# ARM64 com o NDK do Android, se existir no runner
NDK_ROOT="${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-}}"

if [[ -z "$NDK_ROOT" ]]; then
    NDK_ROOT="$(find /usr/local/lib/android/sdk/ndk -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort | tail -n 1 || true)"
fi

NDK_CLANG=""

if [[ -n "$NDK_ROOT" ]]; then
    NDK_CLANG="$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang++"
fi

if [[ -n "$NDK_CLANG" && -x "$NDK_CLANG" ]]; then
    echo "==> ARM64 (NDK): sintaxe dos arquivos prioritarios (limite de 240 s)"
    echo "NDK: $NDK_ROOT"
    check_batch "arm64" "$NDK_CLANG" 240 "${PRIORITY_FILES[@]}"
else
    echo "RESUMO [arm64] NDK do Android nao encontrado, pulado" | tee -a "$CHECK_LOG"
fi

echo
echo "==== INICIO COMPILE ===="
grep '^RESUMO' "$CHECK_LOG" || true
echo
echo "Total de linhas com error: $(grep -c 'error:' "$CHECK_LOG" || true)"
echo
echo "-- arquivos que falharam --"
grep '^RESULT' "$CHECK_LOG" | head -n 20 || true
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

echo "==> Checagem de ligacao: funcoes declaradas x definidas"

DECLARED="$ROOT/generated/declared_funcs.txt"
DEFINED="$ROOT/generated/defined_funcs.txt"
MISSING="$ROOT/generated/missing_funcs.txt"

grep -o 'PPC_EXTERN_FUNC([A-Za-z0-9_]*)' "$OUT_DIR/ppc_recomp_shared.h" \
    | sed 's/PPC_EXTERN_FUNC(//; s/)//' | sort -u > "$DECLARED" || true

grep -h -o 'PPC_WEAK_FUNC([A-Za-z0-9_]*)' "$OUT_DIR"/ppc_recomp.*.cpp \
    | sed 's/PPC_WEAK_FUNC(//; s/)//' | sort -u > "$DEFINED" || true

comm -23 "$DECLARED" "$DEFINED" > "$MISSING" || true

echo
echo "==== INICIO LINK ===="
echo "Funcoes declaradas: $(wc -l < "$DECLARED")"
echo "Funcoes definidas: $(wc -l < "$DEFINED")"
echo "Declaradas sem definicao: $(wc -l < "$MISSING")"
echo "-- primeiras 40 sem definicao --"
head -n 40 "$MISSING" || true
echo "==== FIM LINK ===="
echo

{
    echo "# ==== FILE: compile_check.log ===="
    head -n 4000 "$CHECK_LOG" | sed 's/^/# /'
    echo "# ==== END FILE ===="
    echo "# ==== FILE: missing_funcs.txt ===="
    head -n 3000 "$MISSING" | sed 's/^/# /'
    echo "# ==== END FILE ===="
} >> "$TOML_OUT"

echo "==> Checagens terminaram (isto não gera binário nem prova que o jogo roda)."
