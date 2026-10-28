#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="$ROOT/generated/ppc"
TOML_OUT="$ROOT/generated/BO1_switch_tables.toml"
WORK="$ROOT/generated/ndk_probe"

echo "==> Medindo o custo de compilar o C++ gerado para ARM64 (NDK)"

NDK_ROOT="${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-}}"

if [[ -z "$NDK_ROOT" ]]; then
    NDK_ROOT="$(find /usr/local/lib/android/sdk/ndk -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort | tail -n 1 || true)"
fi

NDK_CLANG=""

if [[ -n "$NDK_ROOT" ]]; then
    NDK_CLANG="$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang++"
fi

if [[ -z "$NDK_CLANG" || ! -x "$NDK_CLANG" ]]; then
    echo "(NDK do Android não encontrado, pulando)"
    exit 0
fi

SIMDE_HEADER="$(find "$ROOT/XenonRecomp/thirdparty/simde" -path '*x86/avx.h' -print -quit)"

if [[ -z "$SIMDE_HEADER" ]]; then
    echo "(simde não encontrado, pulando)"
    exit 0
fi

SIMDE_DIR="$(dirname "$(dirname "$SIMDE_HEADER")")"

mkdir -p "$WORK"

# amostra: o primeiro arquivo e os dois maiores
FIRST="$OUT_DIR/ppc_recomp.0.cpp"
mapfile -t BIGGEST < <(find "$OUT_DIR" -name 'ppc_recomp.*.cpp' -printf '%s %p\n' | sort -rn | head -n 2 | awk '{print $2}' || true)
mapfile -t SAMPLE < <(printf '%s\n' "$FIRST" "${BIGGEST[@]}" | awk '!seen[$0]++')

TOTAL_BYTES="$(find "$OUT_DIR" -name 'ppc_recomp.*.cpp' -printf '%s\n' | awk '{s += $1} END {print s + 0}')"
TOTAL_FILES="$(find "$OUT_DIR" -name 'ppc_recomp.*.cpp' | wc -l)"

: > "$WORK/results.txt"

for opt in O0 O1; do
    for file in "${SAMPLE[@]}"; do
        name="$(basename "$file")"
        obj="$WORK/${name%.cpp}.$opt.o"
        src_bytes="$(stat -c %s "$file")"

        start="$(date +%s)"

        set +e
        timeout 300 "$NDK_CLANG" -std=c++20 -c -fPIC -w "-$opt" \
            -I"$OUT_DIR" -I"$SIMDE_DIR" "$file" -o "$obj" > "$WORK/$name.$opt.log" 2>&1
        status=$?
        set -e

        end="$(date +%s)"
        obj_bytes=0

        if [[ -f "$obj" ]]; then
            obj_bytes="$(stat -c %s "$obj")"
        fi

        echo "$opt $name fonte=$src_bytes objeto=$obj_bytes tempo=$((end - start)) saida=$status" >> "$WORK/results.txt"
    done
done

echo
echo "==== INICIO NDK ===="
echo "NDK: $NDK_ROOT"
echo "Arquivos gerados: $TOTAL_FILES | fonte total: $TOTAL_BYTES bytes"
echo
cat "$WORK/results.txt"
echo
echo "-- estimativa para compilar tudo (proporcional ao tamanho do fonte, 1 nucleo) --"

for opt in O0 O1; do
    awk -v o="$opt" -v total="$TOTAL_BYTES" '
        $1 == o {
            split($3, a, "="); split($5, t, "=")
            bytes += a[2]; secs += t[2]
        }
        END {
            if (bytes > 0) printf "%s: ~%d s (~%.1f min) para %d bytes de fonte\n", o, secs * total / bytes, secs * total / bytes / 60, total
        }' "$WORK/results.txt"
done

echo "-- erros de compilacao (se houver) --"
grep -h 'error:' "$WORK"/*.log 2>/dev/null | cut -c1-200 | head -n 10 || true
echo "==== FIM NDK ===="
echo

{
    echo "# ==== FILE: ndk_probe_results.txt ===="
    sed 's/^/# /' "$WORK/results.txt"
    echo "# ==== END FILE ===="
} >> "$TOML_OUT"
