// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "params/Parameters.h"

namespace subgen::params
{
// The relations of PARAMETERS §1 on a block's snapshot (audio thread, no allocation), the first parameter of a
// relation winning: range_lo gives way to range_hi (range_hi >= 1.2 range_lo), lfe_hp_freq to lfe_lp_freq
// (lfe_lp_freq >= 2 lfe_hp_freq), sub_hp_slope and sub_lp_slope to filt_type (Bessel: at most 48 dB/oct); Linear
// Phase works in Print only, so filt_phase reads Min Phase in Live and Mix.
void sanitise(ParamSnapshot& snapshot);
} // namespace subgen::params
