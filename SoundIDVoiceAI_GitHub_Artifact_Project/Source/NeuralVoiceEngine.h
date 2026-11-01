#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#if USE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

struct VoicePreset
{
    juce::String id;
    juce::String name;
    juce::String category;
    juce::String packName;
    juce::String gender;
    juce::String age;
    juce::StringArray timbreTags;
    juce::String bestInputPitchStr;
    float bestInputPitchMidi = 60.0f;
    bool isUnpitchedDrum = false;
    juce::File onnxModelFile;
};

class NeuralVoiceEngine
{
public:
    static void applyCapturedVoiceCleanup(juce::AudioBuffer<float>& monoBuffer, double sampleRate)
    {
        if (monoBuffer.getNumSamples() == 0)
            return;

        juce::dsp::ProcessSpec spec{ sampleRate, static_cast<juce::uint32>(monoBuffer.getNumSamples()), 1 };

        juce::dsp::IIR::Filter<float> hpf1, hpf2;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 80.0);
        hpf1.coefficients = coeffs;
        hpf2.coefficients = coeffs;
        hpf1.prepare(spec);
        hpf2.prepare(spec);

        juce::dsp::AudioBlock<float> block(monoBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        hpf1.process(ctx);
        hpf2.process(ctx);

        float* samples = monoBuffer.getWritePointer(0);
        const int n = monoBuffer.getNumSamples();
        float env = 0.0f;
        const float attack = std::exp(-1.0f / static_cast<float>(sampleRate * 0.005));
        const float release = std::exp(-1.0f / static_cast<float>(sampleRate * 0.080));
        const float gateThreshold = juce::Decibels::decibelsToGain(-46.0f);

        for (int i = 0; i < n; ++i)
        {
            const float absIn = std::abs(samples[i]);
            env = (absIn > env) ? attack * env + (1.0f - attack) * absIn
                                : release * env + (1.0f - release) * absIn;

            if (env < gateThreshold)
            {
                const float ratio = env / (gateThreshold + 1e-6f);
                samples[i] *= (ratio * ratio);
            }
        }
    }

    static float detectMedianMidiPitch(const juce::AudioBuffer<float>& monoBuffer, double sampleRate)
    {
        const int numSamples = monoBuffer.getNumSamples();
        const int frameSize = 2048;
        const int hopSize = 1024;
        if (numSamples < frameSize)
            return 60.0f;

        const float* data = monoBuffer.getReadPointer(0);
        std::vector<float> voicedMidiNotes;
        const int minLag = static_cast<int>(sampleRate / 800.0);
        const int maxLag = static_cast<int>(sampleRate / 65.0);

        for (int start = 0; start + frameSize < numSamples; start += hopSize)
        {
            float rms = 0.0f;
            for (int i = 0; i < frameSize; ++i)
                rms += data[start + i] * data[start + i];
            rms = std::sqrt(rms / frameSize);
            if (rms < 0.015f)
                continue;

            int bestLag = 0;
            float maxCorr = 0.0f;
            for (int lag = minLag; lag <= maxLag && (start + lag + frameSize / 2) < numSamples; ++lag)
            {
                float corr = 0.0f;
                for (int i = 0; i < frameSize / 2; ++i)
                    corr += data[start + i] * data[start + i + lag];
                if (corr > maxCorr)
                {
                    maxCorr = corr;
                    bestLag = lag;
                }
            }

            if (bestLag > 0)
            {
                const float f0 = static_cast<float>(sampleRate / bestLag);
                const float midi = 69.0f + 12.0f * std::log2(f0 / 440.0f);
                if (midi >= 36.0f && midi <= 84.0f)
                    voicedMidiNotes.push_back(midi);
            }
        }

        if (voicedMidiNotes.empty())
            return 60.0f;

        std::nth_element(voicedMidiNotes.begin(),
                         voicedMidiNotes.begin() + voicedMidiNotes.size() / 2,
                         voicedMidiNotes.end());
        return voicedMidiNotes[voicedMidiNotes.size() / 2];
    }

    static int computeAutoTranspose(float inputMedianMidi, const VoicePreset& preset) noexcept
    {
        if (preset.isUnpitchedDrum)
            return 0;

        const float rawDiff = preset.bestInputPitchMidi - inputMedianMidi;
        const int nearestOctaveSemitones = static_cast<int>(std::round(rawDiff / 12.0f)) * 12;

        if (preset.category == "Creative")
            return juce::jlimit(-24, 24, nearestOctaveSemitones);

        return juce::jlimit(-12, 12, nearestOctaveSemitones);
    }

    static std::shared_ptr<juce::AudioBuffer<float>> runInferencePipeline(
        const juce::AudioBuffer<float>& rawCapturedMono,
        double sampleRate,
        const VoicePreset& preset,
        bool cleanupEnabled,
        int transposeSemitones,
        int unisonVoices,
        float unisonPitchVariancePercent,
        std::atomic<float>& progressOut)
    {
        progressOut.store(0.05f);
        juce::AudioBuffer<float> workingMono(rawCapturedMono);

        if (cleanupEnabled)
            applyCapturedVoiceCleanup(workingMono, sampleRate);

        progressOut.store(0.15f);

        const int numVoices = juce::jlimit(1, 8, unisonVoices);
        const int numSamples = workingMono.getNumSamples();
        auto outputBuffer = std::make_shared<juce::AudioBuffer<float>>(numVoices, numSamples);
        outputBuffer->clear();

        const float varianceNorm = juce::jlimit(0.0f, 1.0f, unisonPitchVariancePercent * 0.01f);

        for (int v = 0; v < numVoices; ++v)
        {
            std::mt19937 rng(1337u + static_cast<unsigned int>(v * 97));
            std::uniform_real_distribution<float> detuneDist(-0.25f, 0.25f);
            const float voiceDetuneSemitones = (v == 0) ? 0.0f : (detuneDist(rng) * varianceNorm);
            const float totalSemitones = (preset.isUnpitchedDrum ? 0.0f : static_cast<float>(transposeSemitones))
                                         + voiceDetuneSemitones;

            synthesizeVoicePass(workingMono.getReadPointer(0),
                                outputBuffer->getWritePointer(v),
                                numSamples,
                                sampleRate,
                                totalSemitones,
                                preset,
                                v);

            progressOut.store(0.15f + 0.85f * (static_cast<float>(v + 1) / static_cast<float>(numVoices)));
        }

        progressOut.store(1.0f);
        return outputBuffer;
    }

private:
    static void synthesizeVoicePass(const float* inMono,
                                    float* outMono,
                                    int numSamples,
                                    double sampleRate,
                                    float totalSemitoneShift,
                                    const VoicePreset& preset,
                                    int voiceSeedIndex)
    {
        const float pitchRatio = std::pow(2.0f, totalSemitoneShift / 12.0f);
        const int grainSize = 1024;
        double readPosA = 0.0;
        double readPosB = grainSize * 0.5;
        const float phaseOffset = static_cast<float>(voiceSeedIndex) * 0.13f;

        juce::dsp::IIR::Filter<float> formantFilter;
        const double formantFreq = (preset.gender == "Female") ? 1150.0 :
                                   (preset.category == "Creative") ? 820.0 : 950.0;
        formantFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate, formantFreq * (1.0 + 0.02 * voiceSeedIndex), 1.8, 1.4f);

        for (int i = 0; i < numSamples; ++i)
        {
            const double envPhase = std::fmod(static_cast<double>(i) / grainSize + phaseOffset, 1.0);
            const float winA = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(envPhase)));
            const float winB = 1.0f - winA;

            const int idxA = juce::jlimit(0, numSamples - 1, static_cast<int>(i - readPosA));
            const int idxB = juce::jlimit(0, numSamples - 1, static_cast<int>(i - readPosB));

            float sample = inMono[idxA] * winA + inMono[idxB] * winB;

            if (preset.category == "Creative" || preset.timbreTags.contains("Raspy"))
                sample = std::tanh(sample * 1.6f) * 0.8f;

            outMono[i] = formantFilter.processSample(sample);

            readPosA += (1.0 - pitchRatio);
            readPosB += (1.0 - pitchRatio);
            if (std::abs(readPosA) >= grainSize) readPosA = 0.0;
            if (std::abs(readPosB) >= grainSize) readPosB = 0.0;
        }
    }
};
