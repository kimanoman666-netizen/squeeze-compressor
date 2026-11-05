#include "PluginEditor.h"

namespace
{
    constexpr int W = 780, H = 500;

    float toDb (float g) { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -100.0f; }
}

SqueezeAudioProcessorEditor::SqueezeAudioProcessorEditor (SqueezeAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      modeSwitch (*p.apvts.getParameter (ParamIDs::mode))
{
    setLookAndFeel (&lnf);

    setupKnob (squeezeKnob, ParamIDs::squeeze, squeezeAtt);
    setupKnob (punchKnob,   ParamIDs::punch,   punchAtt);
    setupKnob (mixKnob,     ParamIDs::mix,     mixAtt);
    setupKnob (outputKnob,  ParamIDs::output,  outputAtt);
    outputKnob.getProperties().set ("bipolar", true);

    addAndMakeVisible (modeSwitch);
    modeSwitch.onChange = [this] (int m) { applyAccent (m); };

    autoGainButton.setClickingTogglesState (true);
    autoGainButton.setTooltip ("Keeps loudness the same before and after compression");
    addAndMakeVisible (autoGainButton);
    autoGainAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::autoGain, autoGainButton);

    const auto& list = SqueezeAudioProcessor::presets();
    for (int i = 0; i < (int) list.size(); ++i)
        presetBox.addItem (list[(size_t) i].name, i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0)
            proc.setCurrentProgram (idx);
    };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (history);
    addAndMakeVisible (curve);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    applyAccent (modeSwitch.getIndex());

    setSize (W, H);
    startTimerHz (60);
}

SqueezeAudioProcessorEditor::~SqueezeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SqueezeAudioProcessorEditor::setupKnob (ui::Knob& k, const char* paramID,
                                              std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& att)
{
    addAndMakeVisible (k);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramID, k);

    if (auto* param = proc.apvts.getParameter (paramID))
        k.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    k.onValueChange = [&k] { k.repaint(); };
}

void SqueezeAudioProcessorEditor::applyAccent (int mode)
{
    accent = ui::accentFor (mode);
    lnf.setAccent (accent);
    inMeter.setAccent (accent);
    outMeter.setAccent (accent);
    repaint();
}

//==============================================================================
void SqueezeAudioProcessorEditor::timerCallback()
{
    const int counter = proc.meterCounter.load();
    if (counter != lastCounter)
    {
        lastCounter = counter;
        lastIn  = toDb (proc.inPeak.exchange (0.0f));
        lastOut = toDb (proc.outPeak.exchange (0.0f));
        lastGr  = proc.grMax.exchange (0.0f);
        lastDet = proc.detDb.exchange (-120.0f);
    }
    else
    {
        // no audio running: let everything fall back gently
        lastIn -= 1.5f; lastOut -= 1.5f; lastGr *= 0.9f; lastDet -= 1.5f;
    }

    grDisplay = lastGr > grDisplay ? lastGr : grDisplay + (lastGr - grDisplay) * 0.12f;

    history.push (lastIn, lastOut, lastGr);
    history.setState (proc.curThr.load(), grDisplay, accent);
    curve.setState (proc.curThr.load(), proc.curRatio.load(), proc.curKnee.load(), lastDet, accent);
    inMeter.setLevel (lastIn);
    outMeter.setLevel (lastOut);

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);

    history.repaint();
    curve.repaint();
    inMeter.repaint();
    outMeter.repaint();
}

//==============================================================================
void SqueezeAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();

    g.setGradientFill (ColourGradient (ui::col::bgTop, 0, 0, ui::col::bgBottom, 0, b.getBottom(), false));
    g.fillAll();

    // soft accent glow behind the big knob
    const auto sq = squeezeKnob.getBounds().toFloat();
    g.setGradientFill (ColourGradient (accent.withAlpha (0.10f), sq.getCentreX(), sq.getCentreY() - 18.0f,
                                       accent.withAlpha (0.0f), sq.getCentreX() + 170.0f, sq.getCentreY() - 18.0f, true));
    g.fillEllipse (sq.getCentreX() - 170.0f, sq.getCentreY() - 188.0f, 340.0f, 340.0f);

    // header
    auto icon = Rectangle<float> (24.0f, 17.0f, 26.0f, 26.0f);
    g.setColour (accent.withAlpha (0.25f));
    g.fillRoundedRectangle (icon.expanded (3.0f), 9.0f);
    g.setGradientFill (ColourGradient (accent.brighter (0.3f), icon.getX(), icon.getY(), accent.darker (0.2f), icon.getRight(), icon.getBottom(), false));
    g.fillRoundedRectangle (icon, 7.0f);

    Path chev;
    const float cy = icon.getCentreY();
    chev.startNewSubPath (icon.getX() + 6.0f, cy - 6.0f);  chev.lineTo (icon.getCentreX() - 1.5f, cy); chev.lineTo (icon.getX() + 6.0f, cy + 6.0f);
    chev.startNewSubPath (icon.getRight() - 6.0f, cy - 6.0f); chev.lineTo (icon.getCentreX() + 1.5f, cy); chev.lineTo (icon.getRight() - 6.0f, cy + 6.0f);
    g.setColour (Colour (0xff0c0d10));
    g.strokePath (chev, PathStrokeType (2.4f, PathStrokeType::curved, PathStrokeType::rounded));

    g.setColour (ui::col::text);
    g.setFont (ui::font (23.0f, true, 0.16f));
    g.drawText ("SQUEEZE", Rectangle<float> (60.0f, 14.0f, 200.0f, 32.0f), Justification::centredLeft);
    const float logoW = GlyphArrangement::getStringWidth (ui::font (23.0f, true, 0.16f), "SQUEEZE");
    g.setColour (ui::col::textDim);
    g.setFont (ui::font (10.0f, true, 0.2f));
    g.drawText ("COMPRESSOR", Rectangle<float> (68.0f + logoW, 22.0f, 120.0f, 16.0f), Justification::centredLeft);

    g.setColour (ui::col::textDim.withAlpha (0.8f));
    g.setFont (ui::font (10.0f, true, 0.15f));
    g.drawText ("PRESET", Rectangle<float> ((float) presetBox.getX() - 62.0f, (float) presetBox.getY(), 54.0f, (float) presetBox.getHeight()),
                Justification::centredRight);

    g.setColour (Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine (58, 0.0f, b.getWidth());

    // meter labels
    g.setFont (ui::font (9.5f, true, 0.15f));
    g.setColour (ui::col::textDim);
    g.drawText ("IN",  inMeter.getBounds().translated (0, inMeter.getHeight() + 4).withHeight (14).expanded (10, 0), Justification::centred);
    g.drawText ("OUT", outMeter.getBounds().translated (0, outMeter.getHeight() + 4).withHeight (14).expanded (10, 0), Justification::centred);

    g.setColour (ui::col::textDim.withAlpha (0.45f));
    g.setFont (ui::font (9.0f));
    g.drawText ("v1.0", Rectangle<float> (b.getWidth() - 60.0f, b.getHeight() - 18.0f, 50.0f, 14.0f), Justification::centredRight);
}

void SqueezeAudioProcessorEditor::resized()
{
    presetBox.setBounds (W - 24 - 210, 15, 210, 30);

    const int dispY = 70, dispH = 192, curveW = 192;
    curve.setBounds (W - 24 - curveW, dispY, curveW, dispH);
    history.setBounds (24, dispY, W - 48 - curveW - 12, dispH);

    modeSwitch.setBounds ((W - 440) / 2, 276, 440, 48);

    const int meterY = 346, meterH = 118;
    inMeter.setBounds  (26, meterY, 12, meterH);
    outMeter.setBounds (W - 38, meterY, 12, meterH);

    autoGainButton.setBounds (56, 392, 112, 28);

    punchKnob.setBounds   (192, 344, 112, 140);
    squeezeKnob.setBounds (310, 334, 160, 160);
    mixKnob.setBounds     (476, 344, 112, 140);
    outputKnob.setBounds  (614, 362, 96, 122);
}
