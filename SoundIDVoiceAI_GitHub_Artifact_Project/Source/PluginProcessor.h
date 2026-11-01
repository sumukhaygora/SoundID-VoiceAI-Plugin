#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginParameters.h"
#include "UnisonRealtimeMixer.h"
#include "CaptureAndCacheManager.h"
#include "NeuralVoiceEngine.h"

class SoundIDVoiceAIProcessor : public juce::AudioProcessor
{
public:
    SoundIDVoiceAIProcessor();
    ~SoundIDVoiceAIProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void toggleCaptureArm(bool shouldCapture);
    void triggerStartProcessing();
    bool isProcessing() const noexcept { return isProcessingFlag.load(); }
    float getProcessingProgress() const noexcept { return processingProgress.load(); }
    bool isMonoOutputBus() const noexcept { return getTotalNumOutputChannels() == 1; }

    const std::vector<VoicePreset>& getPresets() const noexcept { return factoryPresets; }
    int getSelectedPresetIndex() const noexcept { return selectedPresetIndex.load(); }
    void setSelectedPresetIndex(int idx);

    juce::AudioProcessorValueTreeState apvts;
    CaptureAndCacheManager captureManager;

private:
    void initializeFactoryPresets();

    UnisonRealtimeMixer unisonMixer;
    juce::ThreadPool inferencePool{ 1 };

    std::vector<VoicePreset> factoryPresets;
    std::atomic<int> selectedPresetIndex{ 0 };
    std::atomic<bool> captureArmed{ false };
    std::atomic<bool> isProcessingFlag{ false };
    std::atomic<float> processingProgress{ 0.0f };
    int64_t standaloneFallbackPlayhead = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundIDVoiceAIProcessor)
};
