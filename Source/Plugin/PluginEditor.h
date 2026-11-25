#pragma once

#include "DisplayView.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

class VcabLevelEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit VcabLevelEditor (VcabLevelProcessor&);
    ~VcabLevelEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 800, kHeight = 600;

private:
    struct Control
    {
        vcab::P id;
        juce::Label label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    VcabLevelProcessor& proc;
    VCLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltip { this, 600 };
    std::vector<std::unique_ptr<Control>> controls;
    DisplayView display;
    juce::TextButton resetButton, audioSettingsButton;
    juce::Rectangle<int> gateArea, compArea, gainArea, limiterArea; // 操作の帯（paint で下地を描く）

    Control* control (vcab::P id);
    Control& add (vcab::P id, bool withLabel);
    void timerCallback() override;
};
