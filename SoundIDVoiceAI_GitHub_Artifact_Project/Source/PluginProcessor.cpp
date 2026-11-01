#include "PluginProcessor.h"
#include "PluginEditor.h"

SoundIDVoiceAIProcessor::SoundIDVoiceAIProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    initializeFactoryPresets();
}

SoundIDVoiceAIProcessor::~SoundIDVoiceAIProcessor() = default;

void SoundIDVoiceAIProcessor::initializeFactoryPresets()
{
    factoryPresets = {
        { "v_amber",  "Amber (Pop Lead)",      "Voices",    "Factory", "Female", "Adult", { "Warm", "Clear" },   "A4",  69.0f, false, {} },
        { "v_marcus", "Marcus (Soul Baritone)","Voices",    "Factory", "Male",   "Adult", { "Warm", "Raspy" },   "E3",  52.0f, false, {} },
        { "v_chloe",  "Chloe (Indie Airy)",    "Voices",    "Factory", "Female", "Teen",  { "Airy", "Bright" },  "C5",  72.0f, false, {} },
        { "c_cello",  "Studio Solo Violin",    "Creative",  "Factory", "Instrument", "-", { "Warm", "Strings" }, "G4",  67.0f, false, {} },
        { "c_guitar", "Dreadnought Acoustic",  "Creative",  "Factory", "Instrument", "-", { "Bright", "Pluck" }, "C4",  60.0f, false, {} },
        { "c_drums",  "Beatbox Studio Kit",    "Creative",  "Factory", "Instrument", "-", { "Punchy", "Drums" }, "-",   60.0f, true,  {} },
        { "e_kpop1",  "Seoul Idol Lead",       "Expansion", "K-Pop",   "Female", "Teen",  { "Bright", "Clear" }, "G#4", 68.0f, false, {} },
        { "e_rock1",  "Arena Grit Tenor",      "Expansion", "Rock",    "Male",   "Adult", { "Raspy", "Gritty" }, "A3",  57.0f, false, {} }
    };
}

void SoundIDVoiceAIProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    unisonMixer.prepare(sampleRate, samplesPerBlock);
    captureManager.prepare(sampleRate);
    standaloneFallbackPlayhead = 0;
}

void SoundIDVoiceAIProcessor::releaseResources()
{
    unisonMixer.reset();
}

bool SoundIDVoiceAIProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void SoundIDVoiceAIProcessor::toggleCaptureArm(bool shouldCapture)
{
    captureArmed.store(shouldCapture);
    if (!shouldCapture)
        captureManager.stopCapture();
}

void SoundIDVoiceAIProcessor::setSelectedPresetIndex(int idx)
{
    if (idx >= 0 && idx < static_cast<int>(factoryPresets.size()))
        selectedPresetIndex.store(idx);
}

void SoundIDVoiceAIProcessor::triggerStartProcessing()
{
    if (isProcessingFlag.load())
        return;

    captureArmed.store(false);
    captureManager.stopCapture();

    juce::AudioBuffer<float> capturedAudio = captureManager.getCapturedAudioCopy();
    if (capturedAudio.getNumSamples() == 0)
        return;

    isProcessingFlag.store(true);
    processingProgress.store(0.0f);

    const VoicePreset preset = factoryPresets[selectedPresetIndex.load()];
    const bool cleanup = apvts.getRawParameterValue(ParamIDs::captureCleanupEnabled)->load() > 0.5f;
    const bool autoTrans = apvts.getRawParameterValue(ParamIDs::autoTransposeEnabled)->load() > 0.5f;
    int transposeSt = static_cast<int>(apvts.getRawParameterValue(ParamIDs::transposeSemitones)->load());
    const int voices = static_cast<int>(apvts.getRawParameterValue(ParamIDs::unisonVoices)->load());
    const float pitchVar = apvts.getRawParameterValue(ParamIDs::unisonPitchVariance)->load();
    const double sr = getSampleRate();

    inferencePool.addJob([this, capturedAudio = std::move(capturedAudio), preset, cleanup, autoTrans, transposeSt, voices, pitchVar, sr]() mutable
    {
        if (autoTrans)
        {
            const float medianMidi = NeuralVoiceEngine::detectMedianMidiPitch(capturedAudio, sr);
            transposeSt = NeuralVoiceEngine::computeAutoTranspose(medianMidi, preset);
            if (auto* p = apvts.getParameter(ParamIDs::transposeSemitones))
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(transposeSt)));
        }

        auto renderedBuffer = NeuralVoiceEngine::runInferencePipeline(
            capturedAudio, sr, preset, cleanup, transposeSt, voices, pitchVar, processingProgress);

        captureManager.addProcessedTake(preset.name, transposeSt, voices, renderedBuffer);
        isProcessingFlag.store(false);
    });
}

void SoundIDVoiceAIProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    bool hostIsPlaying = true;
    int64_t currentSamplePos = standaloneFallbackPlayhead;

    if (auto* playHead = getPlayHead())
    {
        if (const auto posInfo = playHead->getPosition())
        {
            hostIsPlaying = posInfo->getIsPlaying();
            if (const auto timeInSamples = posInfo->getTimeInSamples())
                currentSamplePos = *timeInSamples;
        }
    }

    if (captureArmed.load() && hostIsPlaying)
    {
        if (!captureManager.isCapturing())
            captureManager.startCapture(currentSamplePos);

        captureManager.pushCaptureSamplesRealtime(buffer.getReadPointer(0), numSamples, currentSamplePos);
    }
    else if (captureManager.isCapturing())
    {
        captureManager.stopCapture();
    }

    const bool aiVoiceOn = apvts.getRawParameterValue(ParamIDs::aiVoiceEnabled)->load() > 0.5f;
    if (aiVoiceOn && hostIsPlaying && !captureArmed.load())
    {
        int64_t takeStart = 0, takeEnd = 0;
        if (auto unisonBuf = captureManager.getActiveUnisonBuffer(takeStart, takeEnd))
        {
            if (currentSamplePos >= takeStart && currentSamplePos < takeEnd)
            {
                const int64_t readOffset = currentSamplePos - takeStart;
                const int activeVoices = static_cast<int>(apvts.getRawParameterValue(ParamIDs::unisonVoices)->load());
                const float timingMs   = apvts.getRawParameterValue(ParamIDs::unisonTimingVariance)->load();
                const float widthPct   = apvts.getRawParameterValue(ParamIDs::unisonStereoWidth)->load();

                unisonMixer.process(*unisonBuf, readOffset, buffer, activeVoices, timingMs, widthPct);
            }
        }
    }

    standaloneFallbackPlayhead += numSamples;
}

juce::AudioProcessorEditor* SoundIDVoiceAIProcessor::createEditor()
{
    return new SoundIDVoiceAIEditor(*this);
}

void SoundIDVoiceAIProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SoundIDVoiceAIProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SoundIDVoiceAIProcessor();
}
