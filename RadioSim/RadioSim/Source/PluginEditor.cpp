#include "PluginEditor.h"
using namespace juce;

static const Colour kBg (0xff17120c), kPanel (0xff241b10), kAmber (0xffffb347), kDim (0xff8a7a5c), kRed (0xffe0402a);

void DialLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                        Slider::SliderStyle, Slider& s)
{
    auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    g.setGradientFill (ColourGradient (Colour (0xff3a2d18), r.getX(), r.getY(), Colour (0xff120d07), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colour (0xff6b5527)); g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 1.5f);
    const double mn = s.getMinimum(), mx = s.getMaximum();
    const bool fm = mx < 500.0;
    const double step = fm ? 0.5 : 20.0, major = fm ? 2.0 : 100.0;
    g.setFont (Font (FontOptions (12.0f)));
    for (double v = std::ceil (mn / step) * step; v <= mx + 1e-6; v += step)
    {
        const float px = (float) jmap (v, mn, mx, (double) minPos, (double) maxPos);
        const bool isMajor = std::fabs (std::fmod (v + 1e-6, major)) < 2e-6 || std::fabs (std::fmod (v + 1e-6, major) - major) < 1e-5;
        g.setColour (isMajor ? kAmber : kDim);
        g.drawLine (px, r.getBottom() - 8.0f, px, r.getBottom() - (isMajor ? 24.0f : 16.0f), 1.0f);
        if (isMajor) g.drawText (String (v, fm ? 0 : 0), Rectangle<float> (px - 22.0f, r.getY() + 8.0f, 44.0f, 16.0f), Justification::centred);
    }
    g.setColour (kRed); g.fillRect (pos - 1.0f, r.getY() + 6.0f, 2.5f, r.getHeight() - 12.0f);
    g.setColour (kRed.withAlpha (0.25f)); g.fillRect (pos - 3.0f, r.getY() + 6.0f, 6.5f, r.getHeight() - 12.0f);
}

Knob::Knob (AudioProcessorValueTreeState& a, const String& id, const String& name)
{
    label.setText (name, dontSendNotification); label.setJustificationType (Justification::centred);
    label.setColour (Label::textColourId, kAmber); label.setFont (Font (FontOptions (12.0f)));
    slider.setColour (Slider::rotarySliderFillColourId, kAmber);
    slider.setColour (Slider::thumbColourId, kAmber);
    slider.setColour (Slider::textBoxTextColourId, Colours::white);
    slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible (label); addAndMakeVisible (slider);
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (a, id, slider);
}
void Knob::resized() { auto b = getLocalBounds(); label.setBounds (b.removeFromTop (16)); slider.setBounds (b); }

Combo::Combo (AudioProcessorValueTreeState& a, const String& id, const String& name)
{
    label.setText (name, dontSendNotification); label.setColour (Label::textColourId, kAmber); label.setFont (Font (FontOptions (12.0f)));
    if (auto* p = dynamic_cast<AudioParameterChoice*> (a.getParameter (id))) box.addItemList (p->choices, 1);
    addAndMakeVisible (label); addAndMakeVisible (box);
    att = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (a, id, box);
}
void Combo::resized() { auto b = getLocalBounds(); label.setBounds (b.removeFromTop (16)); box.setBounds (b.removeFromTop (26)); }

Toggle::Toggle (AudioProcessorValueTreeState& a, const String& id, const String& name)
{
    setButtonText (name); setColour (ToggleButton::textColourId, kAmber); setColour (ToggleButton::tickColourId, kAmber);
    att = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (a, id, *this);
}

#define K(var, id, name) var (p.apvts, id, name)
RadioSimEditor::RadioSimEditor (RadioSimAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      era (p.apvts, "era", "ERA"), region (p.apvts, "region", "REGION"), env (p.apvts, "env", "SURROUNDINGS"),
      antenna (p.apvts, "antenna", "ANTENNA"), ground (p.apvts, "ground", "GROUND"), proc2 (p.apvts, "proc", "STATION PROCESSING"),
      K (distance, "distance", "DISTANCE"), K (power, "power", "POWER"), K (tod, "tod", "TIME OF DAY"), K (storm, "storm", "STORM"),
      K (speed, "speed", "SPEED"), K (mp, "mp", "MULTIPATH"), K (detune, "detune", "DETUNE"), K (adj, "adj", "ADJACENT"),
      K (adj2, "adj2", "2ND ADJ."), K (co, "co", "CO-CHANNEL"), K (impulse, "impulse", "IMPULSE"), K (buzz, "buzz", "LINE BUZZ"),
      K (drive, "drive", "MODULATION"), K (trim, "trim", "SIGNAL TRIM"), K (mix, "mix", "MIX"), K (out, "out", "OUTPUT"),
      stereo (p.apvts, "stereo", "Stereo"), rds (p.apvts, "rds", "RDS"), speaker (p.apvts, "speaker", "Speaker"), smart (p.apvts, "smart", "Smart RX")
{
    setSize (1000, 640);
    // adjacent-station knobs show "Off" at minimum
    for (auto* k : { &adj, &adj2, &co })
        k->slider.textFromValueFunction = [] (double v) { return v <= -59.5 ? String ("Off") : String (v, 1) + " dB"; };
    for (auto* k : { &adj, &adj2, &co }) k->slider.updateText();

    // preset browser (grouped)
    const auto& list = radiosim::factoryPresets();
    String lastGroup;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].group != lastGroup.toStdString()) { presetBox.addSectionHeading (list[i].group); lastGroup = list[i].group; }
        presetBox.addItem (list[i].name, (int) i + 1);
    }
    presetBox.setTextWhenNothingSelected ("Presets...");
    presetBox.onChange = [this] { const int id = presetBox.getSelectedId(); if (id > 0) proc.applyPreset (id - 1); };
    if (proc.apvts.state.hasProperty ("preset")) presetBox.setSelectedId ((int) proc.apvts.state["preset"] + 1, dontSendNotification);
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (+1); };
    bandBox.addItemList (StringArray { "FM", "AM" }, 1);
    bandAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "band", bandBox);

    for (auto* d : { &fmDial, &amDial }) { d->setLookAndFeel (&dialLnf); addAndMakeVisible (d); }
    fmAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "fmFreq", fmDial);
    amAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "amFreq", amDial);
    readout.setJustificationType (Justification::centredLeft); readout.setColour (Label::textColourId, Colours::white);
    readout.setFont (Font (FontOptions (15.0f)));

    for (auto* c : std::initializer_list<Component*> { &presetBox, &bandBox, &prevBtn, &nextBtn, &readout, &era, &region, &env, &antenna, &ground, &proc2,
                                                       &distance, &power, &tod, &storm, &speed, &mp, &detune, &adj, &adj2, &co, &impulse, &buzz,
                                                       &drive, &trim, &mix, &out, &stereo, &rds, &speaker, &smart })
        addAndMakeVisible (c);

    fmOnly = { &antenna, &mp, &speed, &stereo, &rds };
    amOnly = { &ground, &tod, &storm, &buzz };
    startTimerHz (10);
    updateBandState();
}

RadioSimEditor::~RadioSimEditor() { fmDial.setLookAndFeel (nullptr); amDial.setLookAndFeel (nullptr); }

void RadioSimEditor::stepPreset (int d)
{
    const int n = (int) radiosim::factoryPresets().size();
    int cur = presetBox.getSelectedId() - 1; if (cur < 0) cur = d > 0 ? -1 : n;
    cur = (cur + d + n) % n;
    presetBox.setSelectedId (cur + 1);
}

void RadioSimEditor::updateBandState()
{
    const int band = (int) proc.apvts.getRawParameterValue ("band")->load();
    if (band == lastBand) return;
    lastBand = band;
    fmDial.setVisible (band == 0); amDial.setVisible (band == 1);
    for (auto* c : fmOnly) { c->setEnabled (band == 0); c->setAlpha (band == 0 ? 1.0f : 0.3f); }
    for (auto* c : amOnly) { c->setEnabled (band == 1); c->setAlpha (band == 1 ? 1.0f : 0.3f); }
    region.setEnabled (true);
}

void RadioSimEditor::timerCallback()
{
    updateBandState();
    const bool am = lastBand == 1;
    const float f = (float) (am ? proc.apvts.getRawParameterValue ("amFreq")->load() : proc.apvts.getRawParameterValue ("fmFreq")->load());
    String t = am ? String (f, 0) + " kHz" : String (f, 1) + " MHz";
    t << "     wavelength " << String (proc.mWave.load(), 2) << " m     quarter-wave antenna "
      << (proc.mQuarter.load() >= 100.0f ? String (proc.mQuarter.load() / 100.0f, 2) + " m" : String (proc.mQuarter.load(), 0) + " cm");
    if (! am) t << "     radio horizon " << String (proc.mHorizon.load(), 0) << " km";
    else if (proc.mSky.load() > 0.02f) t << "     skywave/groundwave " << String (proc.mSky.load(), 1) << "x";
    readout.setText (t, dontSendNotification);
    repaint (0, 150, getWidth(), 60);
}

void RadioSimEditor::drawBar (Graphics& g, Rectangle<int> r, const String& txt, float v, float lo, float hi, Colour c)
{
    g.setColour (Colour (0xff0d0905)); g.fillRoundedRectangle (r.toFloat(), 4.0f);
    const float p = jlimit (0.0f, 1.0f, (v - lo) / (hi - lo));
    g.setColour (c); g.fillRoundedRectangle (r.toFloat().withWidth (r.getWidth() * p).reduced (1.0f), 3.0f);
    g.setColour (Colours::white); g.setFont (Font (FontOptions (12.0f)));
    g.drawText (txt, r.reduced (6, 0), Justification::centredLeft);
}

void RadioSimEditor::paint (Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    for (auto r : { Rectangle<int> (10, 215, 980, 125), Rectangle<int> (10, 345, 980, 150), Rectangle<int> (10, 500, 980, 135) })
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (kAmber); g.setFont (Font (FontOptions (22.0f, Font::bold)));
    g.drawText ("RADIOSIM", 16, 8, 200, 28, Justification::centredLeft);
    g.setFont (Font (FontOptions (12.0f))); g.setColour (kDim);
    g.drawText ("signal path", 18, 218, 200, 14, Justification::centredLeft);
    g.drawText ("channel impairments", 18, 348, 200, 14, Justification::centredLeft);
    g.drawText ("station / receiver", 18, 503, 200, 14, Justification::centredLeft);
    // meters
    const float field = proc.mField.load(), cnr = proc.mCnr.load();
    drawBar (g, { 640, 160, 350, 20 }, "Field strength  " + String (field, 1) + " dBuV/m", field, -20.0f, 120.0f, Colour (0xff4aa3ff));
    drawBar (g, { 640, 184, 350, 20 }, "Carrier / noise  " + String (cnr, 1) + " dB", cnr, -10.0f, 70.0f,
             cnr > 30.0f ? Colour (0xff3fbf5f) : cnr > 12.0f ? Colour (0xffd9b020) : kRed);
    if (lastBand == 0)
        drawBar (g, { 10, 160, 150, 20 }, proc.mStereo.load() > 0.5f ? "STEREO" : (proc.mStereo.load() > 0.05f ? "BLEND" : "MONO"),
                 proc.mStereo.load(), 0.0f, 1.0f, Colour (0xffb5457a));
}

void RadioSimEditor::resized()
{
    auto b = getLocalBounds().reduced (10);
    auto top = b.removeFromTop (40);
    top.removeFromLeft (190);
    presetBox.setBounds (top.removeFromLeft (360).reduced (0, 6));
    prevBtn.setBounds (top.removeFromLeft (34).reduced (2, 6)); nextBtn.setBounds (top.removeFromLeft (34).reduced (2, 6));
    top.removeFromLeft (20); bandBox.setBounds (top.removeFromLeft (80).reduced (0, 6));
    top.removeFromLeft (20); era.setBounds (top.removeFromLeft (300).withTrimmedTop (0));
    b.removeFromTop (6);
    auto dialRow = b.removeFromTop (100);
    fmDial.setBounds (dialRow); amDial.setBounds (dialRow);
    readout.setBounds (b.removeFromTop (30).withTrimmedLeft (4));
    b.removeFromTop (40);   // meter strip (painted)

    auto place = [] (Rectangle<int> row, std::initializer_list<Component*> cs, int w)
    {
        row = row.reduced (8, 18);
        int x = row.getX();
        for (auto* c : cs) { c->setBounds (x, row.getY(), w - 8, row.getHeight()); x += w; }
    };
    place ({ 10, 215, 980, 125 }, { &distance, &power, &tod, &storm, &speed, &mp, &detune }, 90);
    env.setBounds (650, 235, 160, 44); antenna.setBounds (820, 235, 160, 44);
    ground.setBounds (650, 285, 160, 44);  region.setBounds (820, 285, 160, 44);
    place ({ 10, 345, 980, 150 }, { &adj, &adj2, &co, &impulse, &buzz }, 100);
    proc2.setBounds (540, 370, 200, 44);
    place ({ 10, 500, 980, 135 }, { &drive, &trim, &mix, &out }, 100);
    stereo.setBounds (440, 530, 90, 24); rds.setBounds (440, 560, 90, 24);
    speaker.setBounds (540, 530, 110, 24); smart.setBounds (540, 560, 110, 24);
}
