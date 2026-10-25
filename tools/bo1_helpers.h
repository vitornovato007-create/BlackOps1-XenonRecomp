#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

// Procura as funcoes auxiliares de salvar/restaurar registradores no codigo do BO1
// e textos (strings) escolhidos em tools/search_strings.txt.
// So imprime linhas que comecam com [helpers] ou [strings]. Nao altera a analise.

static uint32_t BO1ReadBE(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void BO1FindOne(Image& image, const char* name, const std::vector<uint32_t>& words)
{
    size_t found = 0;

    for (const auto& section : image.sections)
    {
        if (!(section.flags & SectionFlags_Code) || section.data == nullptr)
        {
            continue;
        }

        size_t count = section.size / 4;

        for (size_t i = 0; i + words.size() <= count; i++)
        {
            bool match = true;

            for (size_t k = 0; k < words.size(); k++)
            {
                if (BO1ReadBE(section.data + (i + k) * 4) != words[k])
                {
                    match = false;
                    break;
                }
            }

            if (match)
            {
                found++;

                if (found <= 10)
                {
                    fmt::println("[helpers] {} = 0x{:X}", name, (uint64_t)(section.base + i * 4));
                }
            }
        }
    }

    fmt::println("[helpers] {}: {} ocorrencia(s)", name, found);
}

static std::string BO1Lower(std::string s)
{
    for (auto& c : s)
    {
        c = (char)std::tolower((unsigned char)c);
    }

    return s;
}

static void BO1FindStrings(Image& image)
{
    std::ifstream in("tools/search_strings.txt");

    if (!in.is_open())
    {
        fmt::println("[strings] tools/search_strings.txt nao encontrado");
        return;
    }

    std::vector<std::string> patterns;
    std::string line;

    while (std::getline(in, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        {
            line.pop_back();
        }

        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        patterns.push_back(line);
    }

    for (const auto& pattern : patterns)
    {
        std::string needle = BO1Lower(pattern);
        size_t found = 0;

        for (const auto& section : image.sections)
        {
            if (section.data == nullptr || section.size < needle.size())
            {
                continue;
            }

            if (section.flags & SectionFlags_Code)
            {
                continue;
            }

            const uint8_t* data = section.data;
            size_t size = section.size;

            for (size_t i = 0; i + needle.size() <= size; i++)
            {
                size_t k = 0;

                while (k < needle.size() && std::tolower(data[i + k]) == (unsigned char)needle[k])
                {
                    k++;
                }

                if (k != needle.size())
                {
                    continue;
                }

                size_t start = i;

                while (start > 0 && i - start < 64 && data[start - 1] >= 0x20 && data[start - 1] < 0x7F)
                {
                    start--;
                }

                size_t end = i + needle.size();

                while (end < size && end - start < 120 && data[end] >= 0x20 && data[end] < 0x7F)
                {
                    end++;
                }

                std::string text((const char*)data + start, end - start);
                fmt::println("[strings] {} 0x{:X} \"{}\"", pattern, (uint64_t)(section.base + start), text);

                found++;

                if (found >= 12)
                {
                    break;
                }
            }

            if (found >= 12)
            {
                break;
            }
        }

        fmt::println("[strings] {}: {} ocorrencia(s) (mostra no maximo 12)", pattern, found);
    }
}

static void BO1FindHelpers(Image& image)
{
    std::vector<uint32_t> restGpr, saveGpr, restFpr, saveFpr;

    for (uint32_t i = 0; i < 8; i++)
    {
        uint32_t step = i * 0x200000u + i * 8u;

        restGpr.push_back(0xE9C1FF68u + step); // ld   r14.., -0x98(r1)..
        saveGpr.push_back(0xF9C1FF68u + step); // std  r14.., -0x98(r1)..
        restFpr.push_back(0xC9CCFF70u + step); // lfd  f14.., -0x90(r12)..
        saveFpr.push_back(0xD9CCFF70u + step); // stfd f14.., -0x90(r12)..
    }

    BO1FindOne(image, "restgprlr_14_address", restGpr);
    BO1FindOne(image, "savegprlr_14_address", saveGpr);
    BO1FindOne(image, "restfpr_14_address", restFpr);
    BO1FindOne(image, "savefpr_14_address", saveFpr);

    BO1FindOne(image, "restvmx_14_address", { 0x3960FEE0u, 0x7DCB60CEu });
    BO1FindOne(image, "savevmx_14_address", { 0x3960FEE0u, 0x7DCB61CEu });
    BO1FindOne(image, "restvmx_64_address", { 0x3960FC00u, 0x100B60CBu });
    BO1FindOne(image, "savevmx_64_address", { 0x3960FC00u, 0x100B61CBu });

    BO1FindStrings(image);
}
