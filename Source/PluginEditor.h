#pragma once

#include "PluginProcessor.h"
#include "UIComponents.h"

class SqueezeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit SqueezeAudioProcessorEditor (SqueezeAudioProcessor&);
    ~SqueezeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyAccent (int mode);
    void setupKnob (ui::Knob& k, const char* paramID,
                    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& att);

    SqueezeAudioProcessor& proc;
    ui::LookAndFeel lnf;

    ui::Knob squeezeKnob { "SQUEEZE", true };
    ui::Knob punchKnob   { "PUNCH / GLUE", false, "PUNCH", "GLUE" };
    ui::Knob mixKnob     { "MIX", false, "DRY", "WET" };
    ui::Knob outputKnob  { "OUTPUT", false };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> squeezeAtt, punchAtt, mixAtt, outputAtt;

    ui::ModeSwitch modeSwitch;
    juce::TextButton autoGainButton { "AUTO GAIN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAtt;
    juce::ComboBox presetBox;

    ui::HistoryDisplay history;
    ui::TransferCurve  curve;
    ui::LevelMeter     inMeter, outMeter;

    juce::Colour accent { ui::col::fat };
    int lastCounter = -1;
    float lastIn = -100.0f, lastOut = -100.0f, lastGr = 0.0f, lastDet = -120.0f;
    float grDisplay = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SqueezeAudioProcessorEditor)
};
