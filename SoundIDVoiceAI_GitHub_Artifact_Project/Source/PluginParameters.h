#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
    inline constexpr const char* aiVoiceEnabled        = "aiVoiceEnabled";
    inline constexpr const char* captureCleanupEnabled = "captureCleanupEnabled";
    inline constexpr const char* autoTransposeEnabled  = "autoTransposeEnabled";
    inline constexpr const char* transposeSemitones    = "transposeSemitones";
    inline constexpr const char* unisonVoices          = "unisonVoices";
    inline constexpr const char* unisonPitchVariance   = "unisonPitchVariance";
    inline constexpr const char* unisonTimingVariance  = "unisonTimingVarianceMs";
    inline constexpr const char* unisonStereoWidth     = "unisonStereoWidth";
    inline constexpr const char* processingMode        = "processingMode";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ParamIDs::aiVoiceEnabled, 1 }, "AI Voice Enable", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ParamIDs::captureCleanupEnabled, 1 }, "Captured Voice Cleanup", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ParamIDs::autoTransposeEnabled, 1 }, "Auto Transpose", true));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ ParamIDs::transposeSemitones, 1 }, "Transpose", -48, 48, 0,
        juce::AudioParameterIntAttributes().withLabel("st")));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ ParamIDs::unisonVoices, 1 }, "Unison Voices", 1, 8, 1));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ParamIDs::unisonPitchVariance, 1 }, "Unison Pitch Variance",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 35.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ParamIDs::unisonTimingVariance, 1 }, "Unison Timing Variance",
        juce::NormalisableRange<float>(2.0f, 50.0f, 0.1f), 15.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ParamIDs::unisonStereoWidth, 1 }, "Unison Stereo Width",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ParamIDs::processingMode, 1 }, "Processing Mode",
        juce::StringArray{ "Perpetual Local", "Pay-As-You-Go Cloud" }, 0));

    return { params.begin(), params.end() };
}
