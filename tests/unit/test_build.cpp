// SPDX-License-Identifier: AGPL-3.0-or-later
// subgen_dsp_tests needs a source before the first DSP task (Task 4); this checks that subgen_dsp brings in the JUCE
// modules the DSP code is allowed to use (juce_core, juce_audio_basics, juce_dsp).
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

TEST_CASE("subgen_dsp links juce_core, juce_audio_basics and juce_dsp", "[build]")
{
    juce::AudioBuffer<double> buffer(1, 16);
    buffer.clear();
    buffer.setSample(0, 0, 1.0);
    CHECK(buffer.getMagnitude(0, 16) == 1.0);

    juce::dsp::FFT fft(4);
    std::array<float, 32> data{};
    data[0] = 1.0f;
    fft.performFrequencyOnlyForwardTransform(data.data());
    for (int k = 0; k <= 8; ++k)
        CHECK(data[static_cast<size_t>(k)] == 1.0f); // an impulse has a flat spectrum
}
