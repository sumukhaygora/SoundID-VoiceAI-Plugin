#include "PluginEditor.h"

SoundIDVoiceAIEditor::SoundIDVoiceAIEditor(SoundIDVoiceAIProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(960, 600);

    modeSelector.addItemList({ "Perpetual Local", "Pay-As-You-Go Cloud" }, 1);
    addAndMakeVisible(modeSelector);
    addAndMakeVisible(aiVoiceToggle);

    captureButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffb82c2c));
    captureButton.onClick = [this]()
    {
        const bool nextState = !processor.captureManager.isCapturing();
        processor.toggleCaptureArm(nextState);
    };
    addAndMakeVisible(captureButton);
    addAndMakeVisible(cleanupToggle);
    captureStatusLabel.setText("Play DAW track to capture up to 5 mins...", juce::dontSendNotification);
    addAndMakeVisible(captureStatusLabel);

    tabVoicesBtn.onClick = [this]() { showingProcessedTab = false; centerListBox.updateContent(); };
    tabProcessedBtn.onClick = [this]() { showingProcessedTab = true; centerListBox.updateContent(); };
    addAndMakeVisible(tabVoicesBtn);
    addAndMakeVisible(tabProcessedBtn);

    centerListBox.setRowHeight(42);
    addAndMakeVisible(centerListBox);

    dragStemBtn.onClick = [this]()
    {
        const auto& hist = processor.captureManager.getHistory();
        const int idx = processor.captureManager.getActiveTakeIndex();
        if (idx >= 0 && idx < static_cast<int>(hist.size()))
        {
            if (hist[idx].stereoDragDropFile.existsAsFile())
                performExternalDragDropOfFiles({ hist[idx].stereoDragDropFile.getFullPathName() }, false);
        }
    };
    addAndMakeVisible(dragStemBtn);

    revealFileBtn.onClick = [this]()
    {
        const auto& hist = processor.captureManager.getHistory();
        const int idx = processor.captureManager.getActiveTakeIndex();
        if (idx >= 0 && idx < static_cast<int>(hist.size()))
            hist[idx].stereoDragDropFile.revealToUser();
    };
    addAndMakeVisible(revealFileBtn);

    auto setupSlider = [this](juce::Slider& s, const juce::String& suffix)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 22);
        s.setTextValueSuffix(suffix);
        addAndMakeVisible(s);
    };

    addAndMakeVisible(autoTransposeToggle);
    setupSlider(transposeSlider, " st");
    setupSlider(unisonVoicesSlider, " voices");
    setupSlider(pitchVarianceSlider, " %");
    setupSlider(timingVarianceSlider, " ms");
    setupSlider(stereoWidthSlider, " %");

    processButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2563eb));
    processButton.onClick = [this]() { processor.triggerStartProcessing(); };
    addAndMakeVisible(processButton);

    modeAttachment           = std::make_unique<ComboAttachment>(processor.apvts, ParamIDs::processingMode, modeSelector);
    aiVoiceAttachment        = std::make_unique<ButtonAttachment>(processor.apvts, ParamIDs::aiVoiceEnabled, aiVoiceToggle);
    cleanupAttachment        = std::make_unique<ButtonAttachment>(processor.apvts, ParamIDs::captureCleanupEnabled, cleanupToggle);
    autoTransposeAttachment  = std::make_unique<ButtonAttachment>(processor.apvts, ParamIDs::autoTransposeEnabled, autoTransposeToggle);
    transposeAttachment      = std::make_unique<SliderAttachment>(processor.apvts, ParamIDs::transposeSemitones, transposeSlider);
    unisonVoicesAttachment   = std::make_unique<SliderAttachment>(processor.apvts, ParamIDs::unisonVoices, unisonVoicesSlider);
    pitchVarianceAttachment  = std::make_unique<SliderAttachment>(processor.apvts, ParamIDs::unisonPitchVariance, pitchVarianceSlider);
    timingVarianceAttachment = std::make_unique<SliderAttachment>(processor.apvts, ParamIDs::unisonTimingVariance, timingVarianceSlider);
    stereoWidthAttachment    = std::make_unique<SliderAttachment>(processor.apvts, ParamIDs::unisonStereoWidth, stereoWidthSlider);

    startTimerHz(15);
}

SoundIDVoiceAIEditor::~SoundIDVoiceAIEditor()
{
    stopTimer();
}

int SoundIDVoiceAIEditor::getNumRows()
{
    return showingProcessedTab
               ? static_cast<int>(processor.captureManager.getHistory().size())
               : static_cast<int>(processor.getPresets().size());
}

void SoundIDVoiceAIEditor::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (selected)
        g.fillAll(juce::Colour(0xff2563eb).withAlpha(0.35f));
    else if (row % 2 == 1)
        g.fillAll(juce::Colour(0xff1e222d));

    g.setColour(juce::Colours::white);
    g.setFont(14.5f);

    if (!showingProcessedTab)
    {
        const auto& presets = processor.getPresets();
        if (row >= 0 && row < static_cast<int>(presets.size()))
        {
            const auto& p = presets[row];
            const juce::String badge = "[" + p.category + " | " + p.packName + "]  " + p.name
                                       + "   (Best Pitch: " + p.bestInputPitchStr + ")";
            g.drawText(badge, 12, 0, width - 24, height, juce::Justification::centredLeft);
        }
    }
    else
    {
        const auto& hist = processor.captureManager.getHistory();
        if (row >= 0 && row < static_cast<int>(hist.size()))
        {
            const auto& h = hist[row];
            const juce::String rowText = h.timestampString + " | " + h.presetName
                                         + " | Transpose: " + juce::String(h.transposeSemitones) + " st"
                                         + " | Unison: " + juce::String(h.unisonVoices) + "x";
            g.drawText(rowText, 12, 0, width - 24, height, juce::Justification::centredLeft);
        }
    }
}

void SoundIDVoiceAIEditor::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (!showingProcessedTab)
        processor.setSelectedPresetIndex(row);
    else
        processor.captureManager.setActiveTakeIndex(row);
}

juce::var SoundIDVoiceAIEditor::getDragSourceDescription(const juce::SparseSet<int>& selectedRows)
{
    if (showingProcessedTab && !selectedRows.isEmpty())
    {
        const int idx = selectedRows[0];
        const auto& hist = processor.captureManager.getHistory();
        if (idx >= 0 && idx < static_cast<int>(hist.size()))
            performExternalDragDropOfFiles({ hist[idx].stereoDragDropFile.getFullPathName() }, false);
    }
    return {};
}

void SoundIDVoiceAIEditor::timerCallback()
{
    const double dur = processor.captureManager.getCapturedDurationSeconds();
    if (processor.captureManager.hasLoopJumpWarning())
    {
        captureStatusLabel.setText("Playhead jump/loop detected! Capture stopped at " + juce::String(dur, 1) + "s",
                                   juce::dontSendNotification);
    }
    else if (processor.captureManager.isCapturing())
    {
        captureButton.setButtonText("■ Stop Capture (" + juce::String(dur, 1) + "s)");
        captureStatusLabel.setText("Capturing DAW audio synced to timeline...", juce::dontSendNotification);
    }
    else
    {
        captureButton.setButtonText("● Capture Audio");
        captureStatusLabel.setText("Captured Region: " + juce::String(dur, 1) + " sec ready", juce::dontSendNotification);
    }

    if (processor.isProcessing())
    {
        const int pct = static_cast<int>(processor.getProcessingProgress() * 100.0f);
        processButton.setButtonText("Processing AI Voice... " + juce::String(pct) + "%");
    }
    else
    {
        processButton.setButtonText(processor.captureManager.getHistory().empty() ? "Start Processing" : "Reprocess Audio");
    }

    transposeSlider.setEnabled(!autoTransposeToggle.getToggleState());
    stereoWidthSlider.setEnabled(!processor.isMonoOutputBus());
    stereoWidthSlider.setTooltip(processor.isMonoOutputBus()
                                     ? "Stereo Width requires a Stereo plugin track."
                                     : "Spreads Unison voices across the stereo field.");
}

void SoundIDVoiceAIEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff12151e));

    g.setColour(juce::Colour(0xff1a1f2c));
    g.fillRoundedRectangle(16.0f, 60.0f, 580.0f, 520.0f, 8.0f);
    g.fillRoundedRectangle(612.0f, 60.0f, 332.0f, 520.0f, 8.0f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("SoundID VoiceAI — Studio Timbre & Unison Workstation", 20, 14, 480, 30, juce::Justification::centredLeft);

    g.setFont(juce::FontOptions(13.5f, juce::Font::bold));
    g.setColour(juce::Colour(0xff94a3b8));
    g.drawText("TRANSPOSE (-48 to +48 st)", 628, 125, 280, 22, juce::Justification::centredLeft);
    g.drawText("UNISON VOICES (Offline: 1-8)", 628, 210, 280, 22, juce::Justification::centredLeft);
    g.drawText("PITCH VARIANCE (Offline %)", 628, 275, 280, 22, juce::Justification::centredLeft);
    g.drawText("TIMING VARIANCE (Real-Time ms)", 628, 350, 280, 22, juce::Justification::centredLeft);
    g.drawText("STEREO WIDTH (Real-Time %)", 628, 415, 280, 22, juce::Justification::centredLeft);
}

void SoundIDVoiceAIEditor::resized()
{
    modeSelector.setBounds(540, 16, 200, 28);
    aiVoiceToggle.setBounds(760, 16, 170, 28);

    captureButton.setBounds(28, 74, 170, 34);
    cleanupToggle.setBounds(212, 74, 185, 34);
    captureStatusLabel.setBounds(400, 74, 185, 34);

    tabVoicesBtn.setBounds(28, 120, 275, 32);
    tabProcessedBtn.setBounds(309, 120, 275, 32);
    centerListBox.setBounds(28, 160, 556, 360);

    dragStemBtn.setBounds(28, 532, 275, 34);
    revealFileBtn.setBounds(309, 532, 275, 34);

    autoTransposeToggle.setBounds(628, 80, 290, 28);
    transposeSlider.setBounds(628, 148, 300, 32);
    unisonVoicesSlider.setBounds(628, 232, 300, 32);
    pitchVarianceSlider.setBounds(628, 298, 300, 32);
    timingVarianceSlider.setBounds(628, 372, 300, 32);
    stereoWidthSlider.setBounds(628, 438, 300, 32);

    processButton.setBounds(628, 505, 300, 52);
}
