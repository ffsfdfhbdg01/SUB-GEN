// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/UiLayout.h"

namespace subgen::ui
{
using enum params::ParamId;

namespace
{
constexpr unsigned bit(int engine) { return 1u << engine; }
constexpr unsigned kTrack = bit(1), kTrigger = bit(2), kHarmonic = bit(3), kHybrid = bit(4);
} // namespace

const std::vector<params::ParamId>& topBarParams()
{
    static const std::vector<params::ParamId> ids{quality, bypass};
    return ids;
}

const std::vector<Group>& mainSections()
{
    static const std::vector<Group> sections{
        {"Source", {range_lo, range_hi, det_thresh, src_mode}},
        {"Generator", {engine, sub1_level, sub2_level, shape}},
        {"Sub", {drive, drive_mode, sub_level, boost_gain, sub_pol}},
        {"Output", {sum_mode, dry_level, mix, out_trim, wet_only, out_mode}},
    };
    return sections;
}

const std::vector<Page>& advancedPages()
{
    static const std::vector<Page> pages{
        {"Source",
         {{"Detection", {src_gain, range_slope}},
          {"Feed", {feed_front, feed_center, feed_surround, feed_height, feed_lfe}},
          {"External key", {sc_use, sc_gain}}}},
        {"Engine",
         {{"Pitch", {base_mode, base_freq}},
          {"Envelope", {follow, env_attack, env_release, dynamics, conf_gate}},
          {"Track", {trk_speed, trk_glide, phase_gen}, kTrack},
          {"Trigger", {trig_thresh, trig_hold, trig_lockout, trig_retrigger, trig_vel}, kTrigger},
          {"Trigger voice", {trig_f_start, trig_pitch_time, trig_attack, trig_decay, trig_phase}, kTrigger},
          {"Harmonics", {harm_amount, harm_balance, harm_max}, kHarmonic | kHybrid},
          {"Hybrid", {hyb_divide, hyb_harmonic}, kHybrid}}},
        {"Dynamics",
         {{"Drive", {drive_balance, drive_tone, drive_auto, drive_pos}},
          {"Boost", {boost_type, boost_freq, boost_q, boost_follow}},
          {"Gate", {gate_on, gate_thresh, gate_range, gate_hold, gate_release}},
          {"Compressor", {comp_on, comp_thresh, comp_ratio, comp_knee, comp_attack, comp_release}},
          {"Compressor detail", {comp_detect, comp_auto_rel, comp_makeup, comp_auto_mu}}}},
        {"EQ",
         {{"Band 1", {eq1_on, eq1_type, eq1_freq, eq1_gain, eq1_q}},
          {"Band 2", {eq2_on, eq2_type, eq2_freq, eq2_gain, eq2_q}},
          {"Band 3", {eq3_on, eq3_type, eq3_freq, eq3_gain, eq3_q}},
          {"Band 4", {eq4_on, eq4_type, eq4_freq, eq4_gain, eq4_q}},
          {"Band 5", {eq5_on, eq5_type, eq5_freq, eq5_gain, eq5_q}}}},
        {"Filters",
         {{"Sub filters", {sub_hp_freq, sub_hp_slope, sub_lp_freq, sub_lp_slope, filt_type, filt_phase}},
          {"Limiter", {lim_on, lim_ceiling, lim_release, lim_tp}},
          {"Phase", {sub_rot, align_obj, align_lock}}}},
        {"Output",
         {{"Mix", {mix_law, align_ms, sub_width}},
          {"Route level", {route_front, route_center, route_surround, route_height, route_lfe}},
          {"Route delay", {route_front_dly, route_center_dly, route_surround_dly, route_height_dly, route_lfe_dly}},
          {"Route polarity", {route_front_pol, route_center_pol, route_surround_pol, route_height_pol, route_lfe_pol}}}},
        {"LFE",
         {{"Band", {lfe_hp_freq, lfe_lp_freq, lfe_lp_slope}},
          {"Level", {lfe_trim, lfe_ceiling, lfe_bleed, lfe_conv}}}},
    };
    return pages;
}
} // namespace subgen::ui
