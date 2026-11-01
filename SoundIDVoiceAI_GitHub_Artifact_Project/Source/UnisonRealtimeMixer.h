#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

class UnisonRealtimeMixer
{
public:
    static constexpr int kMaxVoices = 8;

    void prepare(double sampleRate, int maxBlockSize)
    {
        currentSampleRate = sampleRate;
        const int maxDelaySamples = static_cast<int>( std::ceil(sampleRate * 0.100) ) + maxBlockSize;

        juce::dsp::ProcessSpec spec{ sampleRate, static_cast<juce::uint32>(maxBlockSize), 1 };
        for (int v = 0; v < kMaxVoices; ++v)
        {
            delayLines[v].reset();
            delayLines[v].setMaximumDelayInSamples(maxDelaySamples);
            delayLines[v].prepare(spec);
            lfoPhases[v] = static_cast<float>(v) * (juce::MathConstants<float>::twoPi / kMaxVoices);
        }

        timingVarianceSmoothed.reset(sampleRate, 0.05);
        widthSmoothed.reset(sampleRate, 0.05);
    }

    void reset()
    {
        for (auto& dl : delayLines)
            dl.reset();
    }

    void process(const juce::AudioBuffer<float>& unisonSourceBuffer,
                 int64_t sourceReadOffset,
                 juce::AudioBuffer<float>& outputBuffer,
                 int activeVoices,
                 float timingVarianceMs,
                 float stereoWidthPercent) noexcept
    {
        const int numSamples = outputBuffer.getNumSamples();
        const int numOutChannels = outputBuffer.getNumChannels();
        const int availableVoices = juce::jmin(activeVoices, unisonSourceBuffer.getNumChannels(), kMaxVoices);
        const int64_t totalSourceSamples = unisonSourceBuffer.getNumSamples();

        outputBuffer.clear();
        if (availableVoices <= 0 || sourceReadOffset >= totalSourceSamples)
            return;

        timingVarianceSmoothed.setTargetValue(timingVarianceMs);
        widthSmoothed.setTargetValue(stereoWidthPercent * 0.01f);

        std::array<float, kMaxVoices> basePanPositions{};
        computeSymmetricPanPositions(availableVoices, basePanPositions);

        const float voiceGain = 1.0f / std::sqrt(static_cast<float>(availableVoices));
        const float lfoInc = static_cast<float>(2.0 * juce::MathConstants<double>::pi * 0.35 / currentSampleRate);

        float* outL = outputBuffer.getWritePointer(0);
        float* outR = numOutChannels > 1 ? outputBuffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const int64_t srcIdx = sourceReadOffset + i;
            const bool inBounds = (srcIdx >= 0 && srcIdx < totalSourceSamples);

            const float curTimingMs = timingVarianceSmoothed.getNextValue();
            const float curWidth = (numOutChannels > 1) ? widthSmoothed.getNextValue() : 0.0f;

            float sumL = 0.0f;
            float sumR = 0.0f;

            for (int v = 0; v < availableVoices; ++v)
            {
                const float inSample = inBounds ? unisonSourceBuffer.getSample(v, static_cast<int>(srcIdx)) : 0.0f;
                delayLines[v].pushSample(0, inSample);

                float delaySamples = 0.0f;
                if (v > 0 && availableVoices > 1)
                {
                    const float voiceRatio = static_cast<float>(v) / static_cast<float>(availableVoices - 1);
                    const float lfoMod = 1.0f + 0.08f * std::sin(lfoPhases[v]);
                    lfoPhases[v] += lfoInc;
                    if (lfoPhases[v] > juce::MathConstants<float>::twoPi)
                        lfoPhases[v] -= juce::MathConstants<float>::twoPi;

                    const float voiceDelayMs = curTimingMs * voiceRatio * lfoMod;
                    delaySamples = static_cast<float>((voiceDelayMs * 0.001) * currentSampleRate);
                }

                const float delayedSample = delayLines[v].popSample(0, delaySamples) * voiceGain;

                const float pan = basePanPositions[v] * curWidth;
                const float angle = (pan + 1.0f) * (juce::MathConstants<float>::pi * 0.25f);
                sumL += delayedSample * std::cos(angle);
                sumR += delayedSample * std::sin(angle);
            }

            if (outR != nullptr)
            {
                outL[i] = sumL;
                outR[i] = sumR;
            }
            else
            {
                outL[i] = (sumL + sumR) * 0.7071f;
            }
        }
    }

private:
    static void computeSymmetricPanPositions(int numVoices, std::array<float, kMaxVoices>& pans) noexcept
    {
        pans.fill(0.0f);
        if (numVoices <= 1)
            return;

        if (numVoices % 2 == 1)
        {
            pans[0] = 0.0f;
            const int numPairs = (numVoices - 1) / 2;
            for (int p = 1; p <= numPairs; ++p)
            {
                const float spread = static_cast<float>(p) / static_cast<float>(numPairs);
                pans[2 * p - 1] = -spread;
                pans[2 * p]     = +spread;
            }
        }
        else
        {
            const int numPairs = numVoices / 2;
            for (int p = 1; p <= numPairs; ++p)
            {
                const float spread = static_cast<float>(p) / static_cast<float>(numPairs);
                pans[2 * (p - 1)]     = -spread;
                pans[2 * (p - 1) + 1] = +spread;
            }
        }
    }

    double currentSampleRate = 48000.0;
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>, kMaxVoices> delayLines;
    std::array<float, kMaxVoices> lfoPhases{};
    juce::SmoothedValue<float> timingVarianceSmoothed;
    juce::SmoothedValue<float> widthSmoothed;
};
