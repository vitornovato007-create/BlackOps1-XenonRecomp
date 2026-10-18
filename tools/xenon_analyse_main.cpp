// BO1_PATCHED_MAIN
// Copia ajustada do XenonAnalyse/main.cpp oficial (hedge-dev/XenonRecomp)
// com padroes extras para o compilador usado no Black Ops 1.

#include <cassert>
#include <iterator>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <file.h>
#include <disasm.h>
#include <image.h>
#include <xbox.h>
#include <fmt/core.h>
#include "function.h"

#define SWITCH_ABSOLUTE 0
#define SWITCH_COMPUTED 1
#define SWITCH_BYTEOFFSET 2
#define SWITCH_SHORTOFFSET 3
#define SWITCH_ABSOLUTE_BO1 4
#define SWITCH_SHORTOFFSET_BO1 5

struct SwitchTable
{
    std::vector<size_t> labels{};
    size_t base{};
    size_t defaultLabel{};
    uint32_t r{};
    uint32_t type{};
};

static bool RangeInImage(Image& image, size_t address, size_t bytes)
{
    for (const auto& section : image.sections)
    {
        if (section.data == nullptr)
        {
            continue;
        }

        size_t start = section.base;
        size_t end = start + section.size;

        if (address >= start && address + bytes <= end)
        {
            return true;
        }
    }

    return false;
}

static bool IsCodeAddress(Image& image, size_t address)
{
    for (const auto& section : image.sections)
    {
        if (!(section.flags & SectionFlags_Code))
        {
            continue;
        }

        size_t start = section.base;
        size_t end = start + section.size;

        if (address >= start && address < end)
        {
            return true;
        }
    }

    return false;
}

bool ReadTable(Image& image, SwitchTable& table)
{
    uint32_t pOffset;
    ppc_insn insn;

    auto* code = (uint32_t*)image.Find(table.base);

    if (code == nullptr)
    {
        return false;
    }

    // No BO1 o addi vem depois do rlwinm (code[2]); no padrao original vem em code[1].
    bool bo1 = (table.type == SWITCH_ABSOLUTE_BO1 || table.type == SWITCH_SHORTOFFSET_BO1);

    ppc::Disassemble(code, table.base, insn);
    pOffset = insn.operands[1] << 16;

    ppc::Disassemble(code + (bo1 ? 2 : 1), table.base + (bo1 ? 8 : 4), insn);
    pOffset += insn.operands[2];

    if (table.type == SWITCH_ABSOLUTE || table.type == SWITCH_ABSOLUTE_BO1)
    {
        if (!RangeInImage(image, pOffset, table.labels.size() * 4))
        {
            return false;
        }

        const auto* offsets = (be<uint32_t>*)image.Find(pOffset);

        for (size_t i = 0; i < table.labels.size(); i++)
        {
            table.labels[i] = offsets[i];
        }
    }
    else if (table.type == SWITCH_COMPUTED)
    {
        if (!RangeInImage(image, pOffset, table.labels.size()))
        {
            return false;
        }

        uint32_t base;
        uint32_t shift;
        const auto* offsets = (uint8_t*)image.Find(pOffset);

        ppc::Disassemble(code + 4, table.base + 0x10, insn);
        base = insn.operands[1] << 16;
        ppc::Disassemble(code + 5, table.base + 0x14, insn);
        base += insn.operands[2];

        ppc::Disassemble(code + 3, table.base + 0x0C, insn);
        shift = insn.operands[2];

        for (size_t i = 0; i < table.labels.size(); i++)
        {
            table.labels[i] = base + (offsets[i] << shift);
        }
    }
    else if (table.type == SWITCH_BYTEOFFSET)
    {
        if (!RangeInImage(image, pOffset, table.labels.size()))
        {
            return false;
        }

        const auto* offsets = (uint8_t*)image.Find(pOffset);
        uint32_t base;

        ppc::Disassemble(code + 3, table.base + 0x0C, insn);
        base = insn.operands[1] << 16;
        ppc::Disassemble(code + 4, table.base + 0x10, insn);
        base += insn.operands[2];

        for (size_t i = 0; i < table.labels.size(); i++)
        {
            table.labels[i] = base + offsets[i];
        }
    }
    else if (table.type == SWITCH_SHORTOFFSET || table.type == SWITCH_SHORTOFFSET_BO1)
    {
        if (!RangeInImage(image, pOffset, table.labels.size() * 2))
        {
            return false;
        }

        const auto* offsets = (be<uint16_t>*)image.Find(pOffset);
        uint32_t base;

        ppc::Disassemble(code + 4, table.base + 0x10, insn);
        base = insn.operands[1] << 16;
        ppc::Disassemble(code + 5, table.base + 0x14, insn);
        base += insn.operands[2];

        for (size_t i = 0; i < table.labels.size(); i++)
        {
            table.labels[i] = base + offsets[i];
        }
    }
    else
    {
        assert(false);
        return false;
    }

    return true;
}

void ScanTable(const uint32_t* code, size_t base, SwitchTable& table)
{
    ppc_insn insn;
    uint32_t cr{ (uint32_t)-1 };

    for (int i = 0; i < 32; i++)
    {
        ppc::Disassemble(&code[-i], base - (4 * i), insn);

        if (insn.opcode == nullptr)
        {
            continue;
        }

        if (cr == -1 && (insn.opcode->id == PPC_INST_BGT || insn.opcode->id == PPC_INST_BGTLR || insn.opcode->id == PPC_INST_BLE || insn.opcode->id == PPC_INST_BLELR))
        {
            cr = insn.operands[0];

            if (insn.opcode->operands[1] != 0)
            {
                table.defaultLabel = insn.operands[1];
            }
        }
        else if (cr != -1)
        {
            if (insn.opcode->id == PPC_INST_CMPLWI && insn.operands[0] == cr)
            {
                table.r = insn.operands[1];
                table.labels.resize(insn.operands[2] + 1);
                table.base = base;
                break;
            }
        }
    }
}

void* SearchMask(const void* source, const uint32_t* compare, size_t compareCount, size_t size)
{
    assert(size % 4 == 0);

    uint32_t* src = (uint32_t*)source;
    size_t count = size / 4;

    ppc_insn insn;

    for (size_t i = 0; i < count; i++)
    {
        size_t c = 0;

        for (c = 0; c < compareCount; c++)
        {
            ppc::Disassemble(&src[i + c], 0, insn);

            if (insn.opcode == nullptr || insn.opcode->id != compare[c])
            {
                break;
            }
        }

        if (c == compareCount)
        {
            return &src[i];
        }
    }

    return nullptr;
}

static std::string out;

template<class... Args>
static void println(fmt::format_string<Args...> fmt, Args&&... args)
{
    fmt::vformat_to(std::back_inserter(out), fmt.get(), fmt::make_format_args(args...));
    out += '\n';
};

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        printf("Usage: XenonAnalyse [input XEX file path] [output jump table TOML file path]");
        return EXIT_SUCCESS;
    }

    const auto file = LoadFile(argv[1]);
    auto image = Image::ParseImage(file.data(), file.size());

    auto printTable = [&](const SwitchTable& table)
    {
        println("[[switch]]");
        println("base = 0x{:X}", table.base);
        println("r = {}", table.r);
        println("default = 0x{:X}", table.defaultLabel);
        println("labels = [");

        for (const auto& label : table.labels)
        {
            println("    0x{:X},", label);
        }

        println("]");
        println("");
    };

    std::vector<SwitchTable> switches{};
    std::set<size_t> coveredBctr{};
    size_t acceptedByType[6] = {};
    size_t rejected = 0;

    println("# Generated by XenonAnalyse");

    auto scanPattern = [&](uint32_t* pattern, size_t count, size_t type, size_t bctrIndex)
    {
        for (const auto& section : image.sections)
        {
            if (!(section.flags & SectionFlags_Code))
            {
                continue;
            }

            size_t base = section.base;
            uint8_t* data = section.data;
            uint8_t* dataStart = section.data;
            uint8_t* dataEnd = section.data + section.size;

            while (data < dataEnd && data != nullptr)
            {
                data = (uint8_t*)SearchMask(data, pattern, count, dataEnd - data);

                if (data != nullptr)
                {
                    size_t address = base + (data - dataStart);

                    // O bctr precisa estar exatamente no lugar esperado do padrao.
                    ppc_insn check;
                    ppc::Disassemble((uint32_t*)data + bctrIndex, address + bctrIndex * 4, check);
                    bool hasBctr = check.opcode != nullptr && check.opcode->id == PPC_INST_BCTR;

                    if (hasBctr)
                    {
                        SwitchTable table{};
                        table.type = type;

                        ScanTable((uint32_t*)data, address, table);

                        if (table.base != 0)
                        {
                            size_t bctrAddress = table.base + bctrIndex * 4;

                            if (coveredBctr.count(bctrAddress) == 0)
                            {
                                bool valid = ReadTable(image, table);

                                if (valid)
                                {
                                    for (const auto& label : table.labels)
                                    {
                                        if (!IsCodeAddress(image, label))
                                        {
                                            valid = false;
                                            break;
                                        }
                                    }
                                }

                                if (valid)
                                {
                                    printTable(table);
                                    coveredBctr.insert(bctrAddress);
                                    acceptedByType[type]++;
                                    switches.emplace_back(std::move(table));
                                }
                                else
                                {
                                    rejected++;
                                }
                            }
                        }
                    }

                    data += 4;
                }

                continue;
            }
        }
    };

    // ---- Padroes originais (Sonic Unleashed) ----

    uint32_t absoluteSwitch[] =
    {
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_RLWINM,
        PPC_INST_LWZX,
        PPC_INST_MTCTR,
        PPC_INST_BCTR,
    };

    uint32_t computedSwitch[] =
    {
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_LBZX,
        PPC_INST_RLWINM,
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_ADD,
        PPC_INST_MTCTR,
    };

    uint32_t offsetSwitch[] =
    {
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_LBZX,
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_ADD,
        PPC_INST_MTCTR,
    };

    uint32_t wordOffsetSwitch[] =
    {
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_RLWINM,
        PPC_INST_LHZX,
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_ADD,
        PPC_INST_MTCTR,
    };

    // ---- Padroes novos observados no BO1 ----

    // lis, rlwinm, addi, lwzx, mtctr, bctr
    uint32_t absoluteSwitchBO1[] =
    {
        PPC_INST_LIS,
        PPC_INST_RLWINM,
        PPC_INST_ADDI,
        PPC_INST_LWZX,
        PPC_INST_MTCTR,
        PPC_INST_BCTR,
    };

    // lis, rlwinm, addi, lhzx, lis, addi, (nop, add, mtctr), bctr
    // Usamos so o inicio do padrao e conferimos o bctr no indice 9.
    uint32_t shortOffsetSwitchBO1[] =
    {
        PPC_INST_LIS,
        PPC_INST_RLWINM,
        PPC_INST_ADDI,
        PPC_INST_LHZX,
        PPC_INST_LIS,
        PPC_INST_ADDI,
    };

    println("# ---- ABSOLUTE JUMPTABLE ----");
    scanPattern(absoluteSwitch, std::size(absoluteSwitch), SWITCH_ABSOLUTE, 5);
    scanPattern(absoluteSwitchBO1, std::size(absoluteSwitchBO1), SWITCH_ABSOLUTE_BO1, 5);

    println("# ---- COMPUTED JUMPTABLE ----");
    scanPattern(computedSwitch, std::size(computedSwitch), SWITCH_COMPUTED, 8);

    println("# ---- OFFSETED JUMPTABLE ----");
    scanPattern(offsetSwitch, std::size(offsetSwitch), SWITCH_BYTEOFFSET, 7);
    scanPattern(wordOffsetSwitch, std::size(wordOffsetSwitch), SWITCH_SHORTOFFSET, 8);
    scanPattern(shortOffsetSwitchBO1, std::size(shortOffsetSwitchBO1), SWITCH_SHORTOFFSET_BO1, 9);

    std::ofstream f(argv[2]);
    f.write(out.data(), out.size());
    f.close();

    // ---- Diagnostico (so imprime na tela, nao entra no TOML) ----

    fmt::println("[probe] tabelas aceitas: {} | rejeitadas: {}", switches.size(), rejected);
    fmt::println("[probe] por tipo: absoluta={} computada={} byte={} short={} absoluta_bo1={} short_bo1={}",
        acceptedByType[0], acceptedByType[1], acceptedByType[2],
        acceptedByType[3], acceptedByType[4], acceptedByType[5]);

    size_t totalBctr = 0;
    size_t uncovered = 0;
    std::map<std::string, std::pair<size_t, size_t>> signatures;
    ppc_insn insn;

    for (const auto& section : image.sections)
    {
        if (!(section.flags & SectionFlags_Code) || section.data == nullptr)
        {
            continue;
        }

        const uint32_t* words = (const uint32_t*)section.data;
        size_t wordCount = section.size / 4;
        size_t sectionBase = section.base;

        for (size_t i = 0; i < wordCount; i++)
        {
            ppc::Disassemble(&words[i], 0, insn);

            if (insn.opcode == nullptr || insn.opcode->id != PPC_INST_BCTR)
            {
                continue;
            }

            totalBctr++;

            size_t address = sectionBase + i * 4;

            if (coveredBctr.count(address) != 0)
            {
                continue;
            }

            uncovered++;

            std::string signature;
            size_t start = i >= 6 ? i - 6 : 0;

            for (size_t j = start; j <= i; j++)
            {
                ppc::Disassemble(&words[j], 0, insn);

                if (insn.opcode != nullptr)
                {
                    signature += insn.opcode->name;
                }
                else
                {
                    signature += "?";
                }

                signature += ' ';
            }

            auto& entry = signatures[signature];

            if (entry.first == 0)
            {
                entry.second = address;
            }

            entry.first++;
        }
    }

    fmt::println("[probe] bctr total: {} | cobertos por tabelas: {} | nao cobertos: {}",
        totalBctr, coveredBctr.size(), uncovered);

    std::vector<std::pair<std::string, std::pair<size_t, size_t>>> sorted(signatures.begin(), signatures.end());

    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b)
    {
        return a.second.first > b.second.first;
    });

    size_t limit = std::min<size_t>(sorted.size(), 25);

    for (size_t i = 0; i < limit; i++)
    {
        fmt::println("[probe] {}x exemplo 0x{:X}: {}", sorted[i].second.first, sorted[i].second.second, sorted[i].first);
    }

    return EXIT_SUCCESS;
}
