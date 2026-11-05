#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SqueezeAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = [] (float v, int) { return String (roundToInt (v)) + "%"; };

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::squeeze, 1 }, "Squeeze",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::punch, 1 }, "Punch / Glue",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (v < 47.5f) return "Punch " + String (roundToInt ((50.0f - v) * 2.0f)) + "%";
            if (v > 52.5f) return "Glue "  + String (roundToInt ((v - 50.0f) * 2.0f)) + "%";
            return String ("Balanced");
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::output, 1 }, "Output",
        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return (v > 0.05f ? "+" : "") + String (v, 1) + " dB";
        })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::mode, 1 }, "Mode", StringArray { "Clean", "Fat", "Smooth" }, 1));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::autoGain, 1 }, "Auto Gain", true));

    return layout;
}

const std::vector<Preset>& SqueezeAudioProcessor::presets()
{
    //                         name              mode sq   punch mix   out   auto
    static const std::vector<Preset> list {
        { "Default",              1, 35.f, 50.f, 100.f, 0.f, true },
        { "Vocal - Upfront",      1, 45.f, 40.f, 100.f, 0.f, true },
        { "Vocal - Smooth",       2, 50.f, 70.f, 100.f, 0.f, true },
        { "Vocal - Clean Level",  0, 40.f, 60.f, 100.f, 0.f, true },
        { "808 - Thick",          1, 55.f, 30.f,  80.f, 0.f, true },
        { "Drums - Smash",        1, 80.f, 15.f,  45.f, 0.f, true },
        { "Bus - Glue",           0, 30.f, 85.f, 100.f, 0.f, true },
        { "Master - Gentle",      0, 18.f, 90.f, 100.f, 0.f, true },
        { "Parallel - Fat",       1, 75.f, 35.f,  35.f, 0.f, true },
    };
    return list;
}

//==============================================================================
SqueezeAudioProcessor::SqueezeAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pSqueeze  = apvts.getRawParameterValue (ParamIDs::squeeze);
    pPunch    = apvts.getRawParameterValue (ParamIDs::punch);
    pMix      = apvts.getRawParameterValue (ParamIDs::mix);
    pOutput   = apvts.getRawParameterValue (ParamIDs::output);
    pMode     = apvts.getRawParameterValue (ParamIDs::mode);
    pAutoGain = apvts.getRawParameterValue (ParamIDs::autoGain);
}

bool SqueezeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void SqueezeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto maxCh = (juce::uint32) 2;

    compressor.prepare (sampleRate);

    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) maxCh, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampling->initProcessing ((size_t) samplesPerBlock);

    const float latency = oversampling->getLatencyInSamples();
    setLatencySamples (juce::roundToInt (latency));

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, maxCh };
    mixer.prepare (spec);
    mixer.setMixingRule (juce::dsp::DryWetMixingRule::linear);
    mixer.setWetLatency (latency);

    outGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));

    dcR = (float) (1.0 - juce::MathConstants<double>::twoPi * 8.0 / sampleRate);
    for (int c = 0; c < 2; ++c) dcX1[c] = dcY1[c] = 0.0f;
}

void SqueezeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels(), 2);

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);

    if (n == 0 || numCh == 0 || oversampling == nullptr)
        return;

    const float squeeze01 = pSqueeze->load() * 0.01f;
    const float punch01   = pPunch->load() * 0.01f;
    const float mix01     = pMix->load() * 0.01f;
    const auto  mode      = (sqz::Mode) juce::jlimit (0, 2, (int) pMode->load());
    const bool  autoGain  = pAutoGain->load() > 0.5f;

    float inPk = 0.0f;
    for (int c = 0; c < numCh; ++c)
        inPk = std::max (inPk, buffer.getMagnitude (c, 0, n));

    juce::dsp::AudioBlock<float> block (buffer);
    auto sub = block.getSubsetChannelBlock (0, (size_t) numCh);

    mixer.setWetMixProportion (mix01);
    mixer.pushDrySamples (sub);

    // 1. compression
    compressor.setParameters (squeeze01, punch01, mode, autoGain);
    float grMx = 0.0f, det = -120.0f;
    compressor.process (buffer, numCh, grMx, det);

    // 2. saturation at 2x (always run through the oversampler so latency never changes)
    saturator.setMode (mode, squeeze01);
    auto os = oversampling->processSamplesUp (sub);
    if (saturator.isActive())
    {
        for (size_t c = 0; c < os.getNumChannels(); ++c)
        {
            auto* d = os.getChannelPointer (c);
            for (size_t i = 0; i < os.getNumSamples(); ++i)
                d[i] = saturator.process (d[i]);
        }
    }
    oversampling->processSamplesDown (sub);

    // DC blocker (asymmetric saturation creates a little DC)
    for (int c = 0; c < numCh; ++c)
    {
        auto* d = buffer.getWritePointer (c);
        float x1 = dcX1[c], y1 = dcY1[c];
        for (int i = 0; i < n; ++i)
        {
            const float x = d[i];
            const float y = x - x1 + dcR * y1;
            x1 = x; y1 = y;
            d[i] = y;
        }
        dcX1[c] = x1; dcY1[c] = y1;
    }

    // 3. parallel mix
    mixer.mixWetSamples (sub);

    // 4. output gain
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    auto* const* data = buffer.getArrayOfWritePointers();
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        for (int c = 0; c < numCh; ++c)
            data[c][i] *= g;
    }

    float outPk = 0.0f;
    for (int c = 0; c < numCh; ++c)
        outPk = std::max (outPk, buffer.getMagnitude (c, 0, n));

    atomicMax (inPeak, inPk);
    atomicMax (outPeak, outPk);
    atomicMax (grMax, grMx);
    atomicMax (detDb, det);
    curThr.store (compressor.getThreshold());
    curRatio.store (compressor.getRatio());
    curKnee.store (compressor.getKnee());
    meterCounter.fetch_add (1);
}

//==============================================================================
int SqueezeAudioProcessor::getNumPrograms()
{
    return (int) presets().size();
}

const juce::String SqueezeAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) presets().size()))
        return presets()[(size_t) index].name;
    return {};
}

void SqueezeAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size()) || index == currentProgram)
        return;

    currentProgram = index;
    const auto& p = presets()[(size_t) index];

    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set (ParamIDs::mode,     (float) p.mode);
    set (ParamIDs::squeeze,  p.squeeze);
    set (ParamIDs::punch,    p.punch);
    set (ParamIDs::mix,      p.mix);
    set (ParamIDs::output,   p.output);
    set (ParamIDs::autoGain, p.autoGain ? 1.0f : 0.0f);
}

void SqueezeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SqueezeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) tree.getProperty ("program", 0);
            apvts.replaceState (tree);
        }
    }
}

juce::AudioProcessorEditor* SqueezeAudioProcessor::createEditor()
{
    return new SqueezeAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SqueezeAudioProcessor();
}
