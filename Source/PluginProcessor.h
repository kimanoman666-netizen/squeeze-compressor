#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "CompressorDSP.h"

namespace ParamIDs
{
    inline constexpr const char* squeeze  = "squeeze";
    inline constexpr const char* punch    = "punch";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* output   = "output";
    inline constexpr const char* mode     = "mode";
    inline constexpr const char* autoGain = "autogain";
}

struct Preset
{
    const char* name;
    int   mode;
    float squeeze, punch, mix, output;
    bool  autoGain;
};

class SqueezeAudioProcessor : public juce::AudioProcessor
{
public:
    SqueezeAudioProcessor();
    ~SqueezeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const std::vector<Preset>& presets();

    juce::AudioProcessorValueTreeState apvts;

    // Meter data for the UI. "max since last read": the UI exchanges them back to 0.
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, grMax { 0.0f }, detDb { -120.0f };
    std::atomic<float> curThr { 0.0f }, curRatio { 1.0f }, curKnee { 6.0f };
    std::atomic<int>   meterCounter { 0 };

private:
    static void atomicMax (std::atomic<float>& a, float v) noexcept
    {
        float prev = a.load();
        while (prev < v && ! a.compare_exchange_weak (prev, v)) {}
    }

    std::atomic<float>* pSqueeze  = nullptr;
    std::atomic<float>* pPunch    = nullptr;
    std::atomic<float>* pMix      = nullptr;
    std::atomic<float>* pOutput   = nullptr;
    std::atomic<float>* pMode     = nullptr;
    std::atomic<float>* pAutoGain = nullptr;

    sqz::Compressor compressor;
    sqz::Saturator  saturator;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::DryWetMixer<float> mixer { 512 };
    juce::SmoothedValue<float> outGain;

    float dcX1[2] {}, dcY1[2] {}, dcR = 0.999f;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SqueezeAudioProcessor)
};
