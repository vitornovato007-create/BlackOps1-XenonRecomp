#!/usr/bin/env python3
# Segundo patch do BO1 para XenonRecomp/XenonRecomp/recompiler.cpp
# (rodar DEPOIS de patch_recompiler.py)
#
# 1) adiciona a instrucao vsel128
# 2) segunda passada de extensao de funcoes: percorre TODAS as funcoes (a primeira
#    passada so olhava a funcao de maior base que comeca antes da jump table, e
#    perdia funcoes sobrepostas)
#
# Uso: python3 patch_recompiler2.py caminho/para/recompiler.cpp

import pathlib
import sys

CASES2 = r'''    // BO1_PATCH2: vsel128 (a mascara e o proprio vD) ---------------------------
    case PPC_INST_VSEL128:
        println("\tsimde_mm_store_si128((simde__m128i*){}.u8, simde_mm_or_si128(simde_mm_andnot_si128(simde_mm_load_si128((simde__m128i*){}.u8), simde_mm_load_si128((simde__m128i*){}.u8)), simde_mm_and_si128(simde_mm_load_si128((simde__m128i*){}.u8), simde_mm_load_si128((simde__m128i*){}.u8))));", v(insn.operands[0]), v(insn.operands[0]), v(insn.operands[1]), v(insn.operands[0]), v(insn.operands[2]));
        break;
    // BO1_PATCH2: fim ----------------------------------------------------------

'''

EXTEND2 = r'''    // BO1_PATCH2: segunda passada, percorre todas as funcoes ----------------------
    {
        std::vector<std::pair<size_t, size_t>> tableRanges; // (inicio do padrao, maior destino)

        for (const auto& [tableBase, table] : config.switchTables)
        {
            size_t maxLabel = 0;
            for (size_t label : table.labels)
                maxLabel = std::max<size_t>(maxLabel, label);

            tableRanges.emplace_back(static_cast<size_t>(tableBase), maxLabel);
        }

        std::sort(tableRanges.begin(), tableRanges.end());

        std::sort(functions.begin(), functions.end(), [](auto& lhs, auto& rhs) { return lhs.base < rhs.base; });

        size_t extendedCount2 = 0;
        size_t skippedCount2 = 0;

        for (size_t idx = 0; idx < functions.size(); idx++)
        {
            auto first = std::lower_bound(tableRanges.begin(), tableRanges.end(),
                std::pair<size_t, size_t>(static_cast<size_t>(functions[idx].base), 0));

            for (auto t = first; t != tableRanges.end() && t->first < functions[idx].base + functions[idx].size; ++t)
            {
                size_t fnEnd = functions[idx].base + functions[idx].size;
                size_t maxLabel = t->second;

                if (maxLabel < fnEnd)
                    continue;

                if (maxLabel - fnEnd > 0x20000)
                {
                    fmt::println("[BO1] (2a passada) switch em {:X}: destino {:X} longe demais da funcao {:X}, ignorado", t->first, maxLabel, functions[idx].base);
                    skippedCount2++;
                    continue;
                }

                size_t newEnd = std::max<size_t>(fnEnd, maxLabel + 4);

                for (size_t j = idx + 1; j < functions.size() && functions[j].base <= maxLabel; j++)
                    newEnd = std::max<size_t>(newEnd, functions[j].base + functions[j].size);

                if (newEnd - functions[idx].base > 0x40000)
                {
                    fmt::println("[BO1] (2a passada) funcao {:X} ficaria grande demais ({:X}), ignorada", functions[idx].base, newEnd - functions[idx].base);
                    skippedCount2++;
                    continue;
                }

                fmt::println("[BO1] (2a passada) funcao {:X}: tamanho {:X} -> {:X} (switch em {:X})", functions[idx].base, functions[idx].size, newEnd - functions[idx].base, t->first);

                functions[idx].size = newEnd - functions[idx].base;
                extendedCount2++;
            }
        }

        fmt::println("[BO1] 2a passada: funcoes estendidas: {} | ignoradas: {}", extendedCount2, skippedCount2);
    }
    // BO1_PATCH2: fim ---------------------------------------------------------------

'''

CASES_MARKER = "    case PPC_INST_ADDE:\n"
SORT_MARKER = "    std::sort(functions.begin(), functions.end(), [](auto& lhs, auto& rhs) { return lhs.base < rhs.base; });\n}\n"


def insert_before(text, marker, block):
    count = text.count(marker)
    if count != 1:
        print(f"ERRO: marcador encontrado {count} vez(es) em vez de 1: {marker!r}")
        sys.exit(2)
    return text.replace(marker, block + marker, 1)


def main():
    if len(sys.argv) != 2:
        print("Uso: patch_recompiler2.py caminho/para/recompiler.cpp")
        sys.exit(1)

    path = pathlib.Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")

    if "BO1_PATCH2" in text:
        print("recompiler.cpp ja esta com o patch2 do BO1.")
        return

    if "BO1_PATCH" not in text:
        print("ERRO: rode patch_recompiler.py antes deste.")
        sys.exit(3)

    text = insert_before(text, CASES_MARKER, CASES2)
    text = insert_before(text, SORT_MARKER, EXTEND2)

    path.write_text(text, encoding="utf-8")
    print("patch2 do BO1 aplicado em", path)


if __name__ == "__main__":
    main()
