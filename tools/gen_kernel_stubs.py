#!/usr/bin/env python3
# Gera generated/kernel_stubs.cpp: uma funcao "fraca" para cada funcao do kernel
# que o codigo recompilado chama e que ninguem definiu.
# As implementacoes de verdade, em runtime.cpp, sobrepoem estas.
#
# Uso: python3 tools/gen_kernel_stubs.py generated

import pathlib
import re
import sys


def main():
    if len(sys.argv) != 2:
        print("Uso: gen_kernel_stubs.py generated")
        sys.exit(1)

    generated = pathlib.Path(sys.argv[1])
    ppc = generated / "ppc"
    shared = ppc / "ppc_recomp_shared.h"

    if not shared.is_file():
        print(f"ERRO: {shared} nao encontrado")
        sys.exit(2)

    declared = set(re.findall(r"PPC_EXTERN_FUNC\((\w+)\)", shared.read_text(errors="replace")))

    defined = set()
    for source in sorted(ppc.glob("ppc_recomp.*.cpp")):
        defined |= set(re.findall(r"PPC_WEAK_FUNC\((\w+)\)", source.read_text(errors="replace")))

    missing = sorted(declared - defined)

    lines = [
        "// Gerado por tools/gen_kernel_stubs.py - nao edite.",
        '#include "ppc_recomp_shared.h"',
        "",
        "extern void bo1_import_stub(const char* name, PPCContext& ctx, uint8_t* base);",
        "",
    ]

    for name in missing:
        short = name[len("__imp__"):] if name.startswith("__imp__") else name
        lines.append(f'__attribute__((weak)) PPC_FUNC({name}) {{ bo1_import_stub("{short}", ctx, base); }}')

    out = generated / "kernel_stubs.cpp"
    out.write_text("\n".join(lines) + "\n")

    print(f"declaradas={len(declared)} definidas={len(defined)} sem definicao={len(missing)} -> {out}")


if __name__ == "__main__":
    main()
