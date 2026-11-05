// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

#include "params/Parameters.h"

namespace subgen::ui
{
// Where each parameter lives in the editor (design doc §4): the top bar, the four main sections, and the Advanced
// pages. Each of the 141 parameters has exactly one place.
struct Group
{
    const char* title;
    std::vector<params::ParamId> ids;
    unsigned engines = 0x1f; // bit e: shown when `engine` is e (Divide 0 ... Hybrid 4)
};

struct Page
{
    const char* name;
    std::vector<Group> groups;
};

const std::vector<params::ParamId>& topBarParams();
const std::vector<Group>& mainSections(); // Source, Generator, Sub, Output
const std::vector<Page>& advancedPages(); // Source, Engine, Dynamics, EQ, Filters, Output, LFE

inline bool groupVisible(const Group& g, int engine) { return engine < 0 || ((g.engines >> engine) & 1u) != 0; }

inline constexpr int kEqPage = 3;
} // namespace subgen::ui
