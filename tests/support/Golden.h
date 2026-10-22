// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <map>
#include <span>
#include <string>
#include <vector>

namespace subgen::test
{
// Golden data exported by the prototype (proto/tools/export_golden.py) into tests/golden/*.sgg.
// Format, little-endian: "SGG1", uint32 count, then count times: uint16 name length, UTF-8 name, uint64 n,
// float64[n].
class Golden
{
public:
    // Reads <SUBGEN_GOLDEN_DIR>/<fileName>; throws std::runtime_error when the file is missing or malformed.
    static Golden load(const std::string& fileName);

    // The named array; throws std::out_of_range when it is not in the file.
    std::span<const double> operator[](const std::string& name) const;
    bool contains(const std::string& name) const { return arrays.count(name) != 0; }

private:
    std::map<std::string, std::vector<double>> arrays;
};
} // namespace subgen::test
