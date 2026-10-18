#pragma once

#include <cstdint>
#include <cstddef>
#include <iterator>
#include <string>

// Sonda de diagnostico do BO1.
// Apenas imprime linhas que comecam com [probe]. Nao altera a analise.
static void BO1Probe(Image& image)
{
    fmt::println("[probe] secoes na imagem: {}", std::size(image.sections));

    size_t index = 0;
    for (const auto& section : image.sections)
    {
        bool isCode = (section.flags & SectionFlags_Code) != 0;
        fmt::println("[probe] secao {} base=0x{:X} tamanho=0x{:X} flags=0x{:X} codigo={}",
            index,
            (uint64_t)section.base,
            (uint64_t)section.size,
            (uint64_t)section.flags,
            isCode ? "sim" : "nao");
        index++;
    }

    ppc_insn insn;
    size_t bctrCount = 0;
    size_t mtctrCount = 0;
    size_t totalWords = 0;

    for (const auto& section : image.sections)
    {
        if (!(section.flags & SectionFlags_Code) || section.data == nullptr)
        {
            continue;
        }

        const uint32_t* words = (const uint32_t*)section.data;
        size_t count = section.size / 4;
        totalWords += count;

        for (size_t i = 0; i < count; i++)
        {
            ppc::Disassemble(&words[i], 0, insn);

            if (insn.opcode == nullptr)
            {
                continue;
            }

            if (insn.opcode->id == PPC_INST_BCTR)
            {
                bctrCount++;
            }
            else if (insn.opcode->id == PPC_INST_MTCTR)
            {
                mtctrCount++;
            }
        }
    }

    fmt::println("[probe] instrucoes em secoes de codigo: {}", totalWords);
    fmt::println("[probe] total de mtctr: {}", mtctrCount);
    fmt::println("[probe] total de bctr: {}", bctrCount);

    size_t step = bctrCount > 40 ? bctrCount / 40 : 1;
    size_t seen = 0;

    for (const auto& section : image.sections)
    {
        if (!(section.flags & SectionFlags_Code) || section.data == nullptr)
        {
            continue;
        }

        const uint32_t* words = (const uint32_t*)section.data;
        size_t count = section.size / 4;
        size_t base = section.base;

        for (size_t i = 0; i < count; i++)
        {
            ppc::Disassemble(&words[i], 0, insn);

            if (insn.opcode == nullptr || insn.opcode->id != PPC_INST_BCTR)
            {
                continue;
            }

            if ((seen++ % step) != 0)
            {
                continue;
            }

            std::string line;
            size_t start = i >= 12 ? i - 12 : 0;

            for (size_t j = start; j <= i; j++)
            {
                ppc::Disassemble(&words[j], 0, insn);

                if (insn.opcode != nullptr)
                {
                    line += insn.opcode->name;
                }
                else
                {
                    line += "?";
                }

                line += ' ';
            }

            fmt::println("[probe] bctr 0x{:X}: {}", (uint64_t)(base + i * 4), line);
        }
    }
}
