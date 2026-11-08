#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Glass tuning dial: draws the band scale and a red needle for a horizontal slider.
struct DialLookAndFeel : public juce::LookAndFeel_V4
{
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};

struct Knob : public juce::Component
{
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& name);
    void resized() override;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

struct Combo : public juce::Component
{
    Combo (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& name);
    void resized() override;
    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

struct Toggle : public juce::ToggleButton
{
    Toggle (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& name);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

class RadioSimEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RadioSimEditor (RadioSimAudioProcessor&);
    ~RadioSimEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateBandState();
    void stepPreset (int delta);
    void drawBar (juce::Graphics&, juce::Rectangle<int>, const juce::String&, float value, float lo, float hi, juce::Colour);

    RadioSimAudioProcessor& proc;
    DialLookAndFeel dialLnf;
    juce::ComboBox presetBox, bandBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bandAtt;
    juce::Slider fmDial { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider amDial { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fmAtt, amAtt;
    juce::Label readout;

    Combo era, region, env, antenna, ground, proc2;
    Knob distance, power, tod, storm, speed, mp, detune, adj, adj2, co, impulse, buzz, drive, trim, mix, out;
    Toggle stereo, rds, speaker, smart;
    std::vector<juce::Component*> fmOnly, amOnly;
    int lastBand = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RadioSimEditor)
};
