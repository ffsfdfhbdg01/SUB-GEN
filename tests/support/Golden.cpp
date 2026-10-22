// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/Golden.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

#ifndef SUBGEN_GOLDEN_DIR
#error "SUBGEN_GOLDEN_DIR must name the tests/golden directory"
#endif

namespace subgen::test
{
namespace
{
static_assert(std::endian::native == std::endian::little, "the .sgg reader assumes a little-endian host");

template <typename T>
T readValue(const std::vector<char>& data, std::size_t& pos, const std::string& file)
{
    if (pos + sizeof(T) > data.size())
        throw std::runtime_error(file + ": truncated");
    T value;
    std::memcpy(&value, data.data() + pos, sizeof(T));
    pos += sizeof(T);
    return value;
}
} // namespace

Golden Golden::load(const std::string& fileName)
{
    const std::string path = std::string(SUBGEN_GOLDEN_DIR) + "/" + fileName;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error(path + ": cannot open (run proto/tools/export_golden.py)");
    const std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 8 || std::memcmp(data.data(), "SGG1", 4) != 0)
        throw std::runtime_error(path + ": not an SGG1 file");
    std::size_t pos = 4;
    const auto count = readValue<std::uint32_t>(data, pos, path);
    Golden g;
    for (std::uint32_t c = 0; c < count; ++c)
    {
        const auto nameLength = readValue<std::uint16_t>(data, pos, path);
        if (pos + nameLength > data.size())
            throw std::runtime_error(path + ": truncated");
        std::string name(data.data() + pos, nameLength);
        pos += nameLength;
        const auto n = readValue<std::uint64_t>(data, pos, path);
        if (n > (data.size() - pos) / sizeof(double))
            throw std::runtime_error(path + ": truncated");
        std::vector<double> values(static_cast<std::size_t>(n));
        std::memcpy(values.data(), data.data() + pos, static_cast<std::size_t>(n) * sizeof(double));
        pos += static_cast<std::size_t>(n) * sizeof(double);
        g.arrays.emplace(std::move(name), std::move(values));
    }
    return g;
}

std::span<const double> Golden::operator[](const std::string& name) const
{
    const auto it = arrays.find(name);
    if (it == arrays.end())
        throw std::out_of_range("golden array not found: " + name);
    return it->second;
}
} // namespace subgen::test
