#!/usr/bin/env python3
# Aplica as correcoes do BO1 no XenonRecomp/XenonRecomp/recompiler.cpp
#
# 1) adiciona as instrucoes PPC que o XenonRecomp oficial nao implementa
# 2) estende o tamanho das funcoes cujas jump tables apontam para depois do fim
#    da funcao (o analisador oficial corta a funcao no bctr)
#
# Uso: python3 patch_recompiler.py caminho/para/recompiler.cpp

import pathlib
import sys

CASES = r'''    // BO1_PATCH: instrucoes adicionadas ----------------------------------------
    case PPC_INST_STHU:
        println("\t{} = {} + {}.u32;", ea(), int32_t(insn.operands[1]), r(insn.operands[2]));
        println("\tPPC_STORE_U16({}, {}.u16);", ea(), r(insn.operands[0]));
        println("\t{}.u32 = {};", r(insn.operands[2]), ea());
        break;

    case PPC_INST_STHUX:
        println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        println("\tPPC_STORE_U16({}, {}.u16);", ea(), r(insn.operands[0]));
        println("\t{}.u32 = {};", r(insn.operands[1]), ea());
        break;

    case PPC_INST_STBUX:
        println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        println("\tPPC_STORE_U8({}, {}.u8);", ea(), r(insn.operands[0]));
        println("\t{}.u32 = {};", r(insn.operands[1]), ea());
        break;

    case PPC_INST_LHZU:
        println("\t{} = {} + {}.u32;", ea(), int32_t(insn.operands[1]), r(insn.operands[2]));
        println("\t{}.u64 = PPC_LOAD_U16({});", r(insn.operands[0]), ea());
        println("\t{}.u32 = {};", r(insn.operands[2]), ea());
        break;

    case PPC_INST_LHAU:
        println("\t{} = {} + {}.u32;", ea(), int32_t(insn.operands[1]), r(insn.operands[2]));
        println("\t{}.s64 = int16_t(PPC_LOAD_U16({}));", r(insn.operands[0]), ea());
        println("\t{}.u32 = {};", r(insn.operands[2]), ea());
        break;

    case PPC_INST_LHZUX:
        println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        println("\t{}.u64 = PPC_LOAD_U16({});", r(insn.operands[0]), ea());
        println("\t{}.u32 = {};", r(insn.operands[1]), ea());
        break;

    case PPC_INST_LBZUX:
        println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        println("\t{}.u64 = PPC_LOAD_U8({});", r(insn.operands[0]), ea());
        println("\t{}.u32 = {};", r(insn.operands[1]), ea());
        break;

    case PPC_INST_LDUX:
        println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        println("\t{}.u64 = PPC_LOAD_U64({});", r(insn.operands[0]), ea());
        println("\t{}.u32 = {};", r(insn.operands[1]), ea());
        break;

    case PPC_INST_LFSU:
        printSetFlushMode(false);
        println("\t{} = {} + {}.u32;", ea(), int32_t(insn.operands[1]), r(insn.operands[2]));
        println("\t{}.u32 = PPC_LOAD_U32({});", temp(), ea());
        println("\t{}.f64 = double({}.f32);", f(insn.operands[0]), temp());
        println("\t{}.u32 = {};", r(insn.operands[2]), ea());
        break;

    case PPC_INST_LFSUX:
        printSetFlushMode(false);
        println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        println("\t{}.u32 = PPC_LOAD_U32({});", temp(), ea());
        println("\t{}.f64 = double({}.f32);", f(insn.operands[0]), temp());
        println("\t{}.u32 = {};", r(insn.operands[1]), ea());
        break;

    case PPC_INST_LFDU:
        printSetFlushMode(false);
        println("\t{} = {} + {}.u32;", ea(), int32_t(insn.operands[1]), r(insn.operands[2]));
        println("\t{}.u64 = PPC_LOAD_U64({});", f(insn.operands[0]), ea());
        println("\t{}.u32 = {};", r(insn.operands[2]), ea());
        break;

    case PPC_INST_STFSU:
        printSetFlushMode(false);
        println("\t{}.f32 = float({}.f64);", temp(), f(insn.operands[0]));
        println("\t{} = {} + {}.u32;", ea(), int32_t(insn.operands[1]), r(insn.operands[2]));
        println("\tPPC_STORE_U32({}, {}.u32);", ea(), temp());
        println("\t{}.u32 = {};", r(insn.operands[2]), ea());
        break;

    case PPC_INST_STFSUX:
        printSetFlushMode(false);
        println("\t{}.f32 = float({}.f64);", temp(), f(insn.operands[0]));
        println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        println("\tPPC_STORE_U32({}, {}.u32);", ea(), temp());
        println("\t{}.u32 = {};", r(insn.operands[1]), ea());
        break;

    case PPC_INST_STFDU:
        printSetFlushMode(false);
        println("\t{} = {} + {}.u32;", ea(), int32_t(insn.operands[1]), r(insn.operands[2]));
        println("\tPPC_STORE_U64({}, {}.u64);", ea(), f(insn.operands[0]));
        println("\t{}.u32 = {};", r(insn.operands[2]), ea());
        break;

    case PPC_INST_ADDC:
        println("\t{}.ca = {}.u32 > ~{}.u32;", xer(), r(insn.operands[2]), r(insn.operands[1]));
        println("\t{}.u64 = {}.u64 + {}.u64;", r(insn.operands[0]), r(insn.operands[1]), r(insn.operands[2]));
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.compare<int32_t>({}.s32, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;

    case PPC_INST_ADDME:
        println("\t{}.u8 = ({}.u32 != 0) | {}.ca;", temp(), r(insn.operands[1]), xer());
        println("\t{}.s64 = {}.s64 + {}.ca - 1;", r(insn.operands[0]), r(insn.operands[1]), xer());
        println("\t{}.ca = {}.u8;", xer(), temp());
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.compare<int32_t>({}.s32, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;

    case PPC_INST_SUBFZE:
        println("\t{}.u8 = ({}.u32 == 0) & {}.ca;", temp(), r(insn.operands[1]), xer());
        println("\t{}.u64 = ~{}.u64 + {}.ca;", r(insn.operands[0]), r(insn.operands[1]), xer());
        println("\t{}.ca = {}.u8;", xer(), temp());
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.compare<int32_t>({}.s32, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;

    case PPC_INST_EQV:
        println("\t{}.u64 = ~({}.u64 ^ {}.u64);", r(insn.operands[0]), r(insn.operands[1]), r(insn.operands[2]));
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.compare<int32_t>({}.s32, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;

    case PPC_INST_MULHDU:
        println("\t{}.u64 = uint64_t((__uint128_t({}.u64) * __uint128_t({}.u64)) >> 64);", r(insn.operands[0]), r(insn.operands[1]), r(insn.operands[2]));
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.compare<int32_t>({}.s32, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;

    case PPC_INST_FRSQRTE:
        printSetFlushMode(false);
        println("\t{}.f64 = 1.0 / sqrt({}.f64);", f(insn.operands[0]), f(insn.operands[1]));
        break;

    case PPC_INST_DCBST:
        // no op
        break;

    case PPC_INST_BDZF:
    {
        // bits do CR: 0 = lt, 1 = gt, 2 = eq, 3 = so
        static const char* crBits[] = { "lt", "gt", "eq", "so" };
        println("\t--{}.u64;", ctr());
        println("\tif ({}.u32 == 0 && !{}.{}) goto loc_{:X};", ctr(), cr(insn.operands[0] / 4), crBits[insn.operands[0] % 4], insn.operands[1]);
        break;
    }

    case PPC_INST_BDNZT:
    {
        static const char* crBits[] = { "lt", "gt", "eq", "so" };
        println("\t--{}.u64;", ctr());
        println("\tif ({}.u32 != 0 && {}.{}) goto loc_{:X};", ctr(), cr(insn.operands[0] / 4), crBits[insn.operands[0] % 4], insn.operands[1]);
        break;
    }

    case PPC_INST_LVEHX:
        print("\t{} = (", ea());
        if (insn.operands[1] != 0)
            print("{}.u32 + ", r(insn.operands[1]));
        println("{}.u32) & ~0x1;", r(insn.operands[2]));
        println("\t{}.u16[7 - (({} & 0xF) >> 1)] = PPC_LOAD_U16({});", v(insn.operands[0]), ea(), ea());
        break;

    case PPC_INST_VPKSWSS128:
        println("\tsimde_mm_store_si128((simde__m128i*){}.s16, simde_mm_packs_epi32(simde_mm_load_si128((simde__m128i*){}.s32), simde_mm_load_si128((simde__m128i*){}.s32)));", v(insn.operands[0]), v(insn.operands[2]), v(insn.operands[1]));
        break;

    case PPC_INST_VSUBSHS:
        println("\tsimde_mm_store_si128((simde__m128i*){}.s16, simde_mm_subs_epi16(simde_mm_load_si128((simde__m128i*){}.s16), simde_mm_load_si128((simde__m128i*){}.s16)));", v(insn.operands[0]), v(insn.operands[1]), v(insn.operands[2]));
        break;

    case PPC_INST_VSUBUWS:
        for (size_t i = 0; i < 4; i++)
            println("\t{}.u32[{}] = {}.u32[{}] > {}.u32[{}] ? {}.u32[{}] - {}.u32[{}] : 0u;", v(insn.operands[0]), i, v(insn.operands[2]), i, v(insn.operands[1]), i, v(insn.operands[1]), i, v(insn.operands[2]), i);
        break;

    case PPC_INST_VADDSWS:
        for (size_t i = 0; i < 4; i++)
        {
            println("\t{}.s64 = int64_t({}.s32[{}]) + int64_t({}.s32[{}]);", temp(), v(insn.operands[1]), i, v(insn.operands[2]), i);
            println("\t{}.s32[{}] = {}.s64 > INT_MAX ? INT_MAX : {}.s64 < INT_MIN ? INT_MIN : {}.s64;", v(insn.operands[0]), i, temp(), temp(), temp());
        }
        break;

    case PPC_INST_VCFPUXWS128:
        printSetFlushMode(true);
        for (size_t i = 0; i < 4; i++)
        {
            println("\t{}.f64 = double({}.f32[{}]) * {}.0;", temp(), v(insn.operands[1]), i, 1u << insn.operands[2]);
            println("\t{}.u32[{}] = {}.f64 > 0.0 ? ({}.f64 >= 4294967296.0 ? 0xFFFFFFFFu : uint32_t({}.f64)) : 0u;", v(insn.operands[0]), i, temp(), temp(), temp());
        }
        break;
    // BO1_PATCH: fim das instrucoes adicionadas ---------------------------------

'''

EXTEND = r'''    // BO1_PATCH: estende funcoes cujas jump tables apontam alem do fim da funcao ---
    {
        std::sort(functions.begin(), functions.end(), [](auto& lhs, auto& rhs) { return lhs.base < rhs.base; });

        size_t extendedCount = 0;
        size_t skippedCount = 0;

        for (const auto& [tableBase, table] : config.switchTables)
        {
            size_t maxLabel = 0;
            for (size_t label : table.labels)
                maxLabel = std::max<size_t>(maxLabel, label);

            // funcao que contem o inicio do padrao da jump table
            auto it = std::upper_bound(functions.begin(), functions.end(), static_cast<size_t>(tableBase),
                [](size_t value, const Function& fn) { return value < fn.base; });

            if (it == functions.begin())
                continue;

            size_t idx = static_cast<size_t>(it - functions.begin()) - 1;
            size_t fnEnd = functions[idx].base + functions[idx].size;

            if (tableBase >= fnEnd || maxLabel < fnEnd)
                continue;

            // protecao contra tabelas com destinos absurdos
            if (maxLabel - fnEnd > 0x20000)
            {
                fmt::println("[BO1] switch em {:X}: destino {:X} longe demais do fim da funcao {:X}, ignorado", tableBase, maxLabel, functions[idx].base);
                skippedCount++;
                continue;
            }

            size_t newEnd = std::max<size_t>(fnEnd, maxLabel + 4);

            for (size_t j = idx + 1; j < functions.size() && functions[j].base <= maxLabel; j++)
                newEnd = std::max<size_t>(newEnd, functions[j].base + functions[j].size);

            fmt::println("[BO1] funcao {:X}: tamanho {:X} -> {:X} (switch em {:X})", functions[idx].base, functions[idx].size, newEnd - functions[idx].base, tableBase);

            functions[idx].size = newEnd - functions[idx].base;
            extendedCount++;
        }

        fmt::println("[BO1] funcoes estendidas: {} | ignoradas: {}", extendedCount, skippedCount);
    }
    // BO1_PATCH: fim ---------------------------------------------------------------

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
        print("Uso: patch_recompiler.py caminho/para/recompiler.cpp")
        sys.exit(1)

    path = pathlib.Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")

    if "BO1_PATCH" in text:
        print("recompiler.cpp ja esta com o patch do BO1.")
        return

    text = insert_before(text, CASES_MARKER, CASES)
    text = insert_before(text, SORT_MARKER, EXTEND)

    path.write_text(text, encoding="utf-8")
    print("patch do BO1 aplicado em", path)


if __name__ == "__main__":
    main()
