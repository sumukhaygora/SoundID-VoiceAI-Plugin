#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

struct ProcessedTakeEntry
{
    juce::String uuid;
    juce::String presetName;
    juce::String timestampString;
    int transposeSemitones = 0;
    int unisonVoices = 1;
    int64_t captureStartSample = 0;
    int64_t captureEndSample = 0;
    double sampleRate = 48000.0;
    juce::File multiChannelCacheFile;
    juce::File stereoDragDropFile;
    std::shared_ptr<juce::AudioBuffer<float>> unisonAudioBuffer;
};

class CaptureAndCacheManager : private juce::Thread
{
public:
    static constexpr int kMaxCaptureSeconds = 300;

    CaptureAndCacheManager() : juce::Thread("SoundID_DiskCaptureThread")
    {
        formatManager.registerBasicFormats();
        cacheDirectory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                             .getChildFile("SonarworksAI/SoundIDVoiceAI/Cache");
        cacheDirectory.createDirectory();
        startThread(juce::Thread::Priority::normal);
    }

    ~CaptureAndCacheManager() override
    {
        stopThread(2000);
    }

    void prepare(double sampleRate)
    {
        currentSampleRate = sampleRate;
        const int maxSamples = static_cast<int>( std::ceil(sampleRate * kMaxCaptureSeconds) );
        capturedMonoBuffer.setSize(1, maxSamples, false, true, true);
        fifoBuffer.setSize(1, static_cast<int>(sampleRate * 4.0));
        abstractFifo.setTotalSize(fifoBuffer.getNumSamples());
    }

    void startCapture(int64_t hostStartSample) noexcept
    {
        abstractFifo.reset();
        capturedSampleCount.store(0);
        captureStartSample.store(hostStartSample);
        captureEndSample.store(hostStartSample);
        loopJumpDetected.store(false);
        isCapturingFlag.store(true);
    }

    void stopCapture() noexcept
    {
        isCapturingFlag.store(false);
    }

    void pushCaptureSamplesRealtime(const float* inputMono, int numSamples, int64_t currentHostSample) noexcept
    {
        if (!isCapturingFlag.load())
            return;

        const int64_t expectedSample = captureEndSample.load();
        if (expectedSample > captureStartSample.load() && std::abs(currentHostSample - expectedSample) > numSamples * 2)
        {
            loopJumpDetected.store(true);
            isCapturingFlag.store(false);
            return;
        }

        int start1, size1, start2, size2;
        abstractFifo.prepareToWrite(numSamples, start1, size1, start2, size2);
        if (size1 > 0)
            juce::FloatVectorOperations::copy(fifoBuffer.getWritePointer(0, start1), inputMono, size1);
        if (size2 > 0)
            juce::FloatVectorOperations::copy(fifoBuffer.getWritePointer(0, start2), inputMono + size1, size2);
        abstractFifo.finishedWrite(size1 + size2);

        captureEndSample.store(currentHostSample + numSamples);
        notify();
    }

    juce::AudioBuffer<float> getCapturedAudioCopy() const
    {
        const int total = juce::jmin(capturedSampleCount.load(), capturedMonoBuffer.getNumSamples());
        juce::AudioBuffer<float> copy(1, total);
        if (total > 0)
            copy.copyFrom(0, 0, capturedMonoBuffer, 0, 0, total);
        return copy;
    }

    const ProcessedTakeEntry* addProcessedTake(const juce::String& presetName,
                                               int transposeSt,
                                               int unisonVoices,
                                               std::shared_ptr<juce::AudioBuffer<float>> renderedUnisonBuffer)
    {
        ProcessedTakeEntry entry;
        entry.uuid = juce::Uuid().toString();
        entry.presetName = presetName;
        entry.timestampString = juce::Time::getCurrentTime().formatted("%H:%M:%S");
        entry.transposeSemitones = transposeSt;
        entry.unisonVoices = unisonVoices;
        entry.captureStartSample = captureStartSample.load();
        entry.captureEndSample = captureStartSample.load() + renderedUnisonBuffer->getNumSamples();
        entry.sampleRate = currentSampleRate;
        entry.unisonAudioBuffer = renderedUnisonBuffer;

        const juce::String baseName = presetName.replaceCharacters(" /\\", "___") + "_" + entry.uuid.substring(0, 6);
        entry.multiChannelCacheFile = cacheDirectory.getChildFile(baseName + "_unison.wav");
        entry.stereoDragDropFile    = cacheDirectory.getChildFile(baseName + "_stereo.wav");

        writeWavFile(entry.multiChannelCacheFile, *renderedUnisonBuffer, currentSampleRate);

        juce::AudioBuffer<float> stereoMix(2, renderedUnisonBuffer->getNumSamples());
        stereoMix.clear();
        for (int ch = 0; ch < renderedUnisonBuffer->getNumChannels(); ++ch)
        {
            stereoMix.addFrom(0, 0, *renderedUnisonBuffer, ch, 0, renderedUnisonBuffer->getNumSamples(), 0.707f);
            stereoMix.addFrom(1, 0, *renderedUnisonBuffer, ch, 0, renderedUnisonBuffer->getNumSamples(), 0.707f);
        }
        writeWavFile(entry.stereoDragDropFile, stereoMix, currentSampleRate);

        const juce::ScopedLock sl(historyLock);
        history.push_back(std::move(entry));
        activeTakeIndex.store(static_cast<int>(history.size()) - 1);
        return &history.back();
    }

    std::shared_ptr<juce::AudioBuffer<float>> getActiveUnisonBuffer(int64_t& outStartSample, int64_t& outEndSample) const noexcept
    {
        const int idx = activeTakeIndex.load();
        const juce::ScopedTryLock stl(historyLock);
        if (stl.isLocked() && idx >= 0 && idx < static_cast<int>(history.size()))
        {
            outStartSample = history[idx].captureStartSample;
            outEndSample   = history[idx].captureEndSample;
            return history[idx].unisonAudioBuffer;
        }
        return nullptr;
    }

    const std::vector<ProcessedTakeEntry>& getHistory() const { return history; }
    void setActiveTakeIndex(int index) { activeTakeIndex.store(index); }
    int getActiveTakeIndex() const { return activeTakeIndex.load(); }

    void deleteTake(int index)
    {
        const juce::ScopedLock sl(historyLock);
        if (index >= 0 && index < static_cast<int>(history.size()))
        {
            history[index].multiChannelCacheFile.deleteFile();
            history[index].stereoDragDropFile.deleteFile();
            history.erase(history.begin() + index);
            activeTakeIndex.store(static_cast<int>(history.size()) - 1);
        }
    }

    bool isCapturing() const noexcept { return isCapturingFlag.load(); }
    bool hasLoopJumpWarning() const noexcept { return loopJumpDetected.load(); }
    double getCapturedDurationSeconds() const noexcept { return capturedSampleCount.load() / currentSampleRate; }
    int64_t getCaptureStartSample() const noexcept { return captureStartSample.load(); }
    int64_t getCaptureEndSample() const noexcept { return captureEndSample.load(); }
    juce::File getCacheDirectory() const { return cacheDirectory; }
    void setCacheDirectory(const juce::File& newDir) { cacheDirectory = newDir; cacheDirectory.createDirectory(); }

private:
    void run() override
    {
        while (!threadShouldExit())
        {
            int start1, size1, start2, size2;
            abstractFifo.prepareToRead(abstractFifo.getNumReady(), start1, size1, start2, size2);
            const int totalToRead = size1 + size2;

            if (totalToRead > 0)
            {
                const int currentTotal = capturedSampleCount.load();
                const int maxCapacity = capturedMonoBuffer.getNumSamples();
                const int canWrite = juce::jmin(totalToRead, maxCapacity - currentTotal);

                if (canWrite > 0)
                {
                    const int firstCopy = juce::jmin(size1, canWrite);
                    capturedMonoBuffer.copyFrom(0, currentTotal, fifoBuffer, 0, start1, firstCopy);
                    if (canWrite > firstCopy && size2 > 0)
                        capturedMonoBuffer.copyFrom(0, currentTotal + firstCopy, fifoBuffer, 0, start2, canWrite - firstCopy);

                    capturedSampleCount.fetch_add(canWrite);
                }
                else
                {
                    isCapturingFlag.store(false);
                }

                abstractFifo.finishedRead(totalToRead);
            }
            else
            {
                wait(25);
            }
        }
    }

    void writeWavFile(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        file.deleteFile();
        if (auto fileStream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream()))
        {
            juce::WavAudioFormat wavFormat;
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter>(
                    wavFormat.createWriterFor(fileStream.get(), sampleRate,
                                              static_cast<unsigned int>(buffer.getNumChannels()), 24, {}, 0)))
            {
                fileStream.release();
                writer->writeFromAudioBuffer(buffer, 0, buffer.getNumSamples());
            }
        }
    }

    double currentSampleRate = 48000.0;
    juce::AbstractFifo abstractFifo{ 48000 };
    juce::AudioBuffer<float> fifoBuffer;
    juce::AudioBuffer<float> capturedMonoBuffer;

    std::atomic<bool> isCapturingFlag{ false };
    std::atomic<bool> loopJumpDetected{ false };
    std::atomic<int> capturedSampleCount{ 0 };
    std::atomic<int64_t> captureStartSample{ 0 };
    std::atomic<int64_t> captureEndSample{ 0 };

    juce::AudioFormatManager formatManager;
    juce::File cacheDirectory;
    juce::CriticalSection historyLock;
    std::vector<ProcessedTakeEntry> history;
    std::atomic<int> activeTakeIndex{ -1 };
};
