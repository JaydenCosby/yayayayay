#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "dsp/RadioEngine.h"
#include "dsp/Presets.h"

class RadioSimAudioProcessor : public juce::AudioProcessor
{
public:
    RadioSimAudioProcessor();
    ~RadioSimAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "RadioSim"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void applyPreset (int index);
    radiosim::UserParams readParams() const;

    juce::AudioProcessorValueTreeState apvts;

    // meters (written on the audio thread, read by the editor)
    std::atomic<float> mField { 0.f }, mCnr { 0.f }, mWave { 0.f }, mHorizon { 0.f }, mSky { 0.f }, mStereo { 0.f }, mMute { 1.f }, mQuarter { 0.f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParam (const char* id, float plainValue);

    radiosim::RadioEngine engine;
    juce::AudioBuffer<float> mono2;
    double sr = 44100.0;
    int blockSize = 512;
    bool prepared = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RadioSimAudioProcessor)
};
