#!/usr/bin/env python3
# Terceiro patch do BO1 para XenonRecomp/XenonRecomp/recompiler.cpp
# (rodar DEPOIS de patch_recompiler.py e patch_recompiler2.py)
#
# bdzf e bdnzt: quando o destino do desvio fica fora da funcao (acontece quando
# o "codigo" na verdade e dado, como constantes float), nao existe o rotulo loc_X
# e o C++ gerado nao compila. Agora o desvio vira uma chamada de funcao + return,
# igual aos outros desvios condicionais do XenonRecomp.
#
# Uso: python3 patch_recompiler3.py caminho/para/recompiler.cpp

import pathlib
import sys

OLD_BDZF = r'''        println("\tif ({}.u32 == 0 && !{}.{}) goto loc_{:X};", ctr(), cr(insn.operands[0] / 4), crBits[insn.operands[0] % 4], insn.operands[1]);
'''

NEW_BDZF = r'''        // BO1_PATCH3: destino fora da funcao vira chamada + return
        if (insn.operands[1] < fn.base || insn.operands[1] >= fn.base + fn.size)
        {
            println("\tif ({}.u32 == 0 && !{}.{}) {{", ctr(), cr(insn.operands[0] / 4), crBits[insn.operands[0] % 4]);
            print("\t");
            printFunctionCall(insn.operands[1]);
            println("\t\treturn;");
            println("\t}}");
        }
        else
            println("\tif ({}.u32 == 0 && !{}.{}) goto loc_{:X};", ctr(), cr(insn.operands[0] / 4), crBits[insn.operands[0] % 4], insn.operands[1]);
'''

OLD_BDNZT = r'''        println("\tif ({}.u32 != 0 && {}.{}) goto loc_{:X};", ctr(), cr(insn.operands[0] / 4), crBits[insn.operands[0] % 4], insn.operands[1]);
'''

NEW_BDNZT = r'''        // BO1_PATCH3: destino fora da funcao vira chamada + return
        if (insn.operands[1] < fn.base || insn.operands[1] >= fn.base + fn.size)
        {
            println("\tif ({}.u32 != 0 && {}.{}) {{", ctr(), cr(insn.operands[0] / 4), crBits[insn.operands[0] % 4]);
            print("\t");
            printFunctionCall(insn.operands[1]);
            println("\t\treturn;");
            println("\t}}");
        }
        else
            println("\tif ({}.u32 != 0 && {}.{}) goto loc_{:X};", ctr(), cr(insn.operands[0] / 4), crBits[insn.operands[0] % 4], insn.operands[1]);
'''


def replace_once(text, old, new):
    count = text.count(old)
    if count != 1:
        print(f"ERRO: trecho encontrado {count} vez(es) em vez de 1: {old[:60]!r}")
        sys.exit(2)
    return text.replace(old, new, 1)


def main():
    if len(sys.argv) != 2:
        print("Uso: patch_recompiler3.py caminho/para/recompiler.cpp")
        sys.exit(1)

    path = pathlib.Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")

    if "BO1_PATCH3" in text:
        print("recompiler.cpp ja esta com o patch3 do BO1.")
        return

    if "BO1_PATCH" not in text:
        print("ERRO: rode patch_recompiler.py antes deste.")
        sys.exit(3)

    text = replace_once(text, OLD_BDZF, NEW_BDZF)
    text = replace_once(text, OLD_BDNZT, NEW_BDNZT)

    path.write_text(text, encoding="utf-8")
    print("patch3 do BO1 aplicado em", path)


if __name__ == "__main__":
    main()
