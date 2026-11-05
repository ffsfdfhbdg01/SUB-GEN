// SPDX-License-Identifier: AGPL-3.0-or-later
#include "params/Sanitise.h"

#include <algorithm>

namespace subgen::params
{
namespace
{
constexpr int kBessel = choiceIndex(ParamId::filt_type, "Bessel");
constexpr int kSlope48 = choiceIndex(ParamId::sub_hp_slope, "48 dB/oct");
constexpr int kPrint = choiceIndex(ParamId::quality, "Print");
constexpr int kMinPhase = choiceIndex(ParamId::filt_phase, "Min Phase");
static_assert(kBessel >= 0 && kSlope48 >= 0 && kPrint >= 0 && kMinPhase >= 0, "labels of PARAMETERS.md");
static_assert(choiceIndex(ParamId::sub_lp_slope, "48 dB/oct") == kSlope48, "both slopes list the same values");
} // namespace

void sanitise(ParamSnapshot& s)
{
    s[ParamId::range_lo] = static_cast<float>(std::min<double>(s[ParamId::range_lo], s[ParamId::range_hi] / 1.2));
    s[ParamId::lfe_hp_freq] = static_cast<float>(std::min<double>(s[ParamId::lfe_hp_freq], s[ParamId::lfe_lp_freq] / 2.0));
    if (s.index(ParamId::filt_type) == kBessel)
        for (const ParamId slope : {ParamId::sub_hp_slope, ParamId::sub_lp_slope})
            s[slope] = std::min(s[slope], static_cast<float>(kSlope48));
    if (s.index(ParamId::quality) != kPrint)
        s[ParamId::filt_phase] = static_cast<float>(kMinPhase);
}
} // namespace subgen::params
