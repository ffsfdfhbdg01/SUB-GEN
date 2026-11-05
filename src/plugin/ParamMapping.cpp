// SPDX-License-Identifier: AGPL-3.0-or-later
#include "plugin/ParamMapping.h"

namespace subgen::plugin
{
using params::ParamId;

namespace
{
int slopeOf(const params::ParamSnapshot& s, ParamId id) { return 12 << std::clamp(s.index(id), 0, 3); } // 12, 24, 48, 96

double gainOf(const params::ParamSnapshot& s, ParamId id) { return s.gain(id); }

double fraction(const params::ParamSnapshot& s, ParamId id) { return static_cast<double>(s[id]) / 100.0; }
} // namespace

Quality qualityOf(const params::ParamSnapshot& s) { return static_cast<Quality>(std::clamp(s.index(ParamId::quality), 0, 2)); }

bool linearPhaseOf(const params::ParamSnapshot& s) { return s.index(ParamId::filt_phase) == 1; }

engines::EngineType engineOf(const params::ParamSnapshot& s)
{
    return static_cast<engines::EngineType>(std::clamp(s.index(ParamId::engine), 0, 4));
}

engines::EngineControl engineControlOf(const params::ParamSnapshot& s, bool keyTrigger)
{
    engines::EngineControl c;
    c.sub1Gain = gainOf(s, ParamId::sub1_level);
    c.sub2Gain = gainOf(s, ParamId::sub2_level);
    c.shape = fraction(s, ParamId::shape);
    c.follow = s.index(ParamId::follow) == 0;
    c.envAttackMs = s[ParamId::env_attack];
    c.envReleaseMs = s[ParamId::env_release];
    c.dynamics = fraction(s, ParamId::dynamics);
    c.confGate = fraction(s, ParamId::conf_gate);
    c.baseFixed = s.index(ParamId::base_mode) == 1;
    c.baseFreqHz = s[ParamId::base_freq];
    c.rangeLoHz = s[ParamId::range_lo];
    c.rangeHiHz = s[ParamId::range_hi];
    c.rangeSlope = 12 << std::clamp(s.index(ParamId::range_slope), 0, 2);
    c.detThreshDb = s[ParamId::det_thresh];
    c.keyMode = keyTrigger ? engines::KeyMode::Trigger : engines::KeyMode::None;
    c.trkSpeed = fraction(s, ParamId::trk_speed);
    c.trkGlideMs = s[ParamId::trk_glide];
    c.phaseGen = static_cast<engines::PhaseGen>(std::clamp(s.index(ParamId::phase_gen), 0, 2));
    c.pitchHold = s.on(ParamId::pitch_hold);
    c.trigThreshDb = s[ParamId::trig_thresh];
    c.trigHoldMs = s[ParamId::trig_hold];
    c.trigLockoutMs = s[ParamId::trig_lockout];
    c.trigFStartHz = s[ParamId::trig_f_start];
    c.trigPitchTimeMs = s[ParamId::trig_pitch_time];
    c.trigAttackMs = s[ParamId::trig_attack];
    c.trigDecayMs = s[ParamId::trig_decay];
    c.trigPhaseDeg = s[ParamId::trig_phase];
    c.trigVel = fraction(s, ParamId::trig_vel);
    c.trigLegato = s.index(ParamId::trig_retrigger) == 1;
    c.harmAmount = fraction(s, ParamId::harm_amount);
    c.harmBalance = fraction(s, ParamId::harm_balance);
    c.harmMax = 3 + std::clamp(s.index(ParamId::harm_max), 0, 2);
    c.hybDivide = fraction(s, ParamId::hyb_divide);
    c.hybHarmonic = fraction(s, ParamId::hyb_harmonic);
    return engines::clampControl(c);
}

routing::SourceParams sourceParamsOf(const params::ParamSnapshot& s, bool keyAvailable)
{
    routing::SourceParams p;
    p.feedGain = {gainOf(s, ParamId::feed_front), gainOf(s, ParamId::feed_center), gainOf(s, ParamId::feed_surround),
                  gainOf(s, ParamId::feed_height), gainOf(s, ParamId::feed_lfe)};
    p.srcGain = gainOf(s, ParamId::src_gain);
    p.mode = static_cast<routing::SourceMode>(std::clamp(s.index(ParamId::src_mode), 0, 3));
    p.keyMode = keyAvailable ? static_cast<routing::KeyMode>(std::clamp(s.index(ParamId::sc_use), 0, 2)) : routing::KeyMode::Off;
    p.keyGain = gainOf(s, ParamId::sc_gain);
    p.bandLoHz = s[ParamId::range_lo];
    p.bandHiHz = s[ParamId::range_hi];
    return p;
}

routing::RouterParams routerParamsOf(const params::ParamSnapshot& s)
{
    routing::RouterParams p;
    p.mode = static_cast<routing::OutMode>(std::clamp(s.index(ParamId::out_mode), 0, 4));
    p.classGain = {gainOf(s, ParamId::route_front), gainOf(s, ParamId::route_center), gainOf(s, ParamId::route_surround),
                   gainOf(s, ParamId::route_height), gainOf(s, ParamId::route_lfe)};
    p.classDelayMs = {s[ParamId::route_front_dly], s[ParamId::route_center_dly], s[ParamId::route_surround_dly],
                      s[ParamId::route_height_dly], s[ParamId::route_lfe_dly]};
    p.classInvert = {s.on(ParamId::route_front_pol), s.on(ParamId::route_center_pol), s.on(ParamId::route_surround_pol),
                     s.on(ParamId::route_height_pol), s.on(ParamId::route_lfe_pol)};
    p.width = fraction(s, ParamId::sub_width);
    p.lfeTrimGain = gainOf(s, ParamId::lfe_trim);
    p.lfeBleedGain = gainOf(s, ParamId::lfe_bleed);
    p.lfeHpHz = s[ParamId::lfe_hp_freq];
    p.lfeLpHz = s[ParamId::lfe_lp_freq];
    p.lfeLpSlope = slopeOf(s, ParamId::lfe_lp_slope);
    p.family = static_cast<dsp::FilterFamily>(std::clamp(s.index(ParamId::filt_type), 0, 2));
    return p;
}

routing::SumParams sumParamsOf(const params::ParamSnapshot& s)
{
    routing::SumParams p;
    p.mixMode = s.index(ParamId::sum_mode) == 1;
    p.dryGain = gainOf(s, ParamId::dry_level);
    p.mix = fraction(s, ParamId::mix);
    p.equalPower = s.index(ParamId::mix_law) == 0;
    p.wetOnly = s.on(ParamId::wet_only);
    p.outTrimGain = gainOf(s, ParamId::out_trim);
    return p;
}

chain::SubChainParams subChainParamsOf(const params::ParamSnapshot& s, double fSub1Hz, bool engineHasPitch, bool lfeMode)
{
    chain::SubChainParams p;
    p.levelGain = gainOf(s, ParamId::sub_level);
    p.boost.gainDb = s[ParamId::boost_gain];
    p.boost.type = static_cast<chain::BoostType>(std::clamp(s.index(ParamId::boost_type), 0, 1));
    p.boost.freqHz = s[ParamId::boost_freq];
    p.boost.follow = s.on(ParamId::boost_follow);
    p.boost.q = s[ParamId::boost_q];
    p.boost.fSub1Hz = fSub1Hz;
    p.boost.engineHasPitch = engineHasPitch;
    p.gate.on = s.on(ParamId::gate_on);
    p.gate.threshDb = s[ParamId::gate_thresh];
    p.gate.rangeDb = s[ParamId::gate_range];
    p.gate.holdMs = s[ParamId::gate_hold];
    p.gate.releaseMs = s[ParamId::gate_release];
    p.gate.fSub1Hz = fSub1Hz;
    p.drive.drivePct = s[ParamId::drive];
    p.drive.mode = static_cast<chain::DriveMode>(std::clamp(s.index(ParamId::drive_mode), 0, 4));
    p.drive.balance = s[ParamId::drive_balance];
    p.drive.toneHz = s[ParamId::drive_tone];
    p.drive.autoGain = s.on(ParamId::drive_auto);
    p.drive.fSub1Hz = fSub1Hz;
    p.drivePostComp = s.index(ParamId::drive_pos) == 1;
    p.comp.on = s.on(ParamId::comp_on);
    p.comp.threshDb = s[ParamId::comp_thresh];
    p.comp.ratio = s[ParamId::comp_ratio];
    p.comp.kneeDb = s[ParamId::comp_knee];
    p.comp.attackMs = s[ParamId::comp_attack];
    p.comp.releaseMs = s[ParamId::comp_release];
    p.comp.autoRelease = s.on(ParamId::comp_auto_rel);
    p.comp.detector = s.index(ParamId::comp_detect) == 1 ? chain::Detector::Rms : chain::Detector::Peak;
    p.comp.makeupDb = s[ParamId::comp_makeup];
    p.comp.autoMakeup = s.on(ParamId::comp_auto_mu);
    p.comp.fSub1Hz = fSub1Hz;
    constexpr std::array<std::array<ParamId, 5>, 5> eq{{
        {ParamId::eq1_on, ParamId::eq1_type, ParamId::eq1_freq, ParamId::eq1_gain, ParamId::eq1_q},
        {ParamId::eq2_on, ParamId::eq2_type, ParamId::eq2_freq, ParamId::eq2_gain, ParamId::eq2_q},
        {ParamId::eq3_on, ParamId::eq3_type, ParamId::eq3_freq, ParamId::eq3_gain, ParamId::eq3_q},
        {ParamId::eq4_on, ParamId::eq4_type, ParamId::eq4_freq, ParamId::eq4_gain, ParamId::eq4_q},
        {ParamId::eq5_on, ParamId::eq5_type, ParamId::eq5_freq, ParamId::eq5_gain, ParamId::eq5_q}}};
    for (size_t b = 0; b < eq.size(); ++b)
        p.eq.bands[b] = {s.on(eq[b][0]), static_cast<chain::EqType>(std::clamp(s.index(eq[b][1]), 0, 2)), s[eq[b][2]],
                         s[eq[b][3]], s[eq[b][4]]};
    p.filters.hpHz = s[ParamId::sub_hp_freq];
    p.filters.hpSlope = slopeOf(s, ParamId::sub_hp_slope);
    p.filters.lpHz = s[ParamId::sub_lp_freq];
    p.filters.lpSlope = slopeOf(s, ParamId::sub_lp_slope);
    p.filters.family = static_cast<dsp::FilterFamily>(std::clamp(s.index(ParamId::filt_type), 0, 2));
    p.filters.lfeMode = lfeMode;
    p.filters.lfeHpHz = s[ParamId::lfe_hp_freq];
    p.filters.lfeLpHz = s[ParamId::lfe_lp_freq];
    p.filters.lfeLpSlope = slopeOf(s, ParamId::lfe_lp_slope);
    p.filters.linearPhase = linearPhaseOf(s);
    p.phase.invert = s.on(ParamId::sub_pol);
    p.phase.rotationDeg = s[ParamId::sub_rot];
    p.limiter.on = s.on(ParamId::lim_on);
    p.limiter.ceilingDb = lfeMode ? s[ParamId::lfe_ceiling] : s[ParamId::lim_ceiling];
    p.limiter.releaseMs = s[ParamId::lim_release];
    p.limiter.truePeak = s.on(ParamId::lim_tp);
    return p;
}
} // namespace subgen::plugin
