// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "chain/SubChain.h"
#include "core/Quality.h"
#include "engines/Engine.h"
#include "engines/EngineHost.h"
#include "params/Parameters.h"
#include "routing/Router.h"
#include "routing/SourceMixer.h"
#include "routing/Summation.h"

namespace subgen::plugin
{
// A sanitised parameter snapshot (PARAMETERS.md) as the stages' parameter structs: dB to linear gains (-60 dB Off to
// 0), percentages to fractions, choices to their enums, slopes to dB/oct. No allocation.
Quality qualityOf(const params::ParamSnapshot& s);
bool linearPhaseOf(const params::ParamSnapshot& s); // filt_phase (sanitised: Print only)
engines::EngineType engineOf(const params::ParamSnapshot& s);
engines::EngineControl engineControlOf(const params::ParamSnapshot& s, bool keyTrigger);
routing::SourceParams sourceParamsOf(const params::ParamSnapshot& s, bool keyAvailable);
routing::RouterParams routerParamsOf(const params::ParamSnapshot& s);
routing::SumParams sumParamsOf(const params::ParamSnapshot& s);
// fSub1Hz: the stream's Sub -1 (the engine's, or base_freq before its first estimate); lfeMode: the router resolved LFE
chain::SubChainParams subChainParamsOf(const params::ParamSnapshot& s, double fSub1Hz, bool engineHasPitch, bool lfeMode);
} // namespace subgen::plugin
