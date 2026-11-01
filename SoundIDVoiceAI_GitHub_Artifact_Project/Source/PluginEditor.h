#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class SoundIDVoiceAIEditor : public juce::AudioProcessorEditor,
                             public juce::DragAndDropContainer,
                             private juce::Timer,
                             private juce::ListBoxModel
{
public:
    explicit SoundIDVoiceAIEditor(SoundIDVoiceAIProcessor&);
    ~SoundIDVoiceAIEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& currentlySelectedRows) override;

private:
    void timerCallback() override;

    SoundIDVoiceAIProcessor& processor;

    juce::ComboBox modeSelector;
    juce::ToggleButton aiVoiceToggle{ "AI Voice Active" };

    juce::TextButton captureButton{ "● Capture Audio" };
    juce::ToggleButton cleanupToggle{ "Captured Voice Cleanup" };
    juce::Label captureStatusLabel;

    juce::TextButton tabVoicesBtn{ "Voices / Creative / Packs" };
    juce::TextButton tabProcessedBtn{ "Processed History" };
    bool showingProcessedTab = false;
    juce::ListBox centerListBox{ "BrowserList", this };
    juce::TextButton dragStemBtn{ "Drag Active WAV to DAW" };
    juce::TextButton revealFileBtn{ "Reveal File in Finder/Explorer" };

    juce::ToggleButton autoTransposeToggle{ "Auto Transpose" };
    juce::Slider transposeSlider;
    juce::Slider unisonVoicesSlider;
    juce::Slider pitchVarianceSlider;
    juce::Slider timingVarianceSlider;
    juce::Slider stereoWidthSlider;
    juce::TextButton processButton{ "Start Processing" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ComboAttachment>  modeAttachment;
    std::unique_ptr<ButtonAttachment> aiVoiceAttachment;
    std::unique_ptr<ButtonAttachment> cleanupAttachment;
    std::unique_ptr<ButtonAttachment> autoTransposeAttachment;
    std::unique_ptr<SliderAttachment> transposeAttachment;
    std::unique_ptr<SliderAttachment> unisonVoicesAttachment;
    std::unique_ptr<SliderAttachment> pitchVarianceAttachment;
    std::unique_ptr<SliderAttachment> timingVarianceAttachment;
    std::unique_ptr<SliderAttachment> stereoWidthAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundIDVoiceAIEditor)
};
