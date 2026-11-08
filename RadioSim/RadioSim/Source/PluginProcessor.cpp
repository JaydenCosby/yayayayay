#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace juce;

namespace
{
const StringArray kBand { "FM", "AM" };
StringArray eraNames() { StringArray s; for (auto* n : radiosim::kEraNames) s.add (n); return s; }
StringArray envNames() { StringArray s; for (auto* n : radiosim::kEnvNames) s.add (n); return s; }
StringArray antNames() { StringArray s { "Era default" }; for (auto* n : radiosim::kAntNames) s.add (n); return s; }
}

AudioProcessorValueTreeState::ParameterLayout RadioSimAudioProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    auto F = [&l] (const char* id, const char* name, float lo, float hi, float step, float def, float skew, const char* unit)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, step, skew), def,
                                                      AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto C = [&l] (const char* id, const char* name, const StringArray& items, int def)
    { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    auto B = [&l] (const char* id, const char* name, bool def)
    { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    C ("band", "Band", kBand, 0);
    C ("era", "Era", eraNames(), 8);
    C ("region", "Region", StringArray { "Americas (75us, 200 kHz)", "Europe/Asia (50us, 9 kHz)" }, 0);
    F ("fmFreq", "FM Frequency", 87.5f, 108.0f, 0.1f, 98.7f, 1.0f, "MHz");
    F ("amFreq", "AM Frequency", 530.0f, 1710.0f, 10.0f, 1010.0f, 1.0f, "kHz");
    F ("distance", "Distance", 0.1f, 3000.0f, 0.1f, 25.0f, 0.3f, "km");
    F ("power", "Station Power", 0.1f, 250.0f, 0.1f, 50.0f, 0.4f, "kW");
    C ("env", "Surroundings", envNames(), 1);
    C ("antenna", "Antenna", antNames(), 0);
    C ("ground", "Ground (AM)", StringArray { "Poor (1 mS/m)", "Average (5)", "Good (10)", "Sea water" }, 1);
    F ("tod", "Time of Day (AM)", 0.0f, 1.0f, 0.01f, 0.0f, 1.0f, "");
    F ("storm", "Storm", 0.0f, 1.0f, 0.01f, 0.0f, 1.0f, "");
    F ("speed", "Speed", 0.0f, 130.0f, 1.0f, 0.0f, 1.0f, "km/h");
    F ("mp", "Multipath", 0.0f, 1.0f, 0.01f, 0.3f, 1.0f, "");
    F ("adj", "Adjacent Station", -60.0f, 40.0f, 0.5f, -60.0f, 1.0f, "dB");
    F ("adj2", "2nd Adjacent", -60.0f, 40.0f, 0.5f, -60.0f, 1.0f, "dB");
    F ("co", "Co-channel", -60.0f, 30.0f, 0.5f, -60.0f, 1.0f, "dB");
    F ("impulse", "Impulse Noise", 0.0f, 1.0f, 0.01f, 0.0f, 1.0f, "");
    F ("buzz", "Line Buzz (AM)", 0.0f, 1.0f, 0.01f, 0.0f, 1.0f, "");
    F ("detune", "Detune", -1.0f, 1.0f, 0.01f, 0.0f, 1.0f, "");
    B ("stereo", "Stereo", true);
    B ("rds", "RDS", true);
    F ("drive", "Modulation", 0.2f, 1.3f, 0.01f, 0.85f, 1.0f, "");
    C ("proc", "Station Processing", StringArray { "Era", "Off", "Light", "Medium", "Heavy" }, 0);
    B ("speaker", "Speaker", true);
    B ("smart", "Smart Receiver", true);
    F ("trim", "Signal Trim", -30.0f, 30.0f, 0.1f, 0.0f, 1.0f, "dB");
    F ("mix", "Mix", 0.0f, 1.0f, 0.01f, 1.0f, 1.0f, "");
    F ("out", "Output", -24.0f, 12.0f, 0.1f, 0.0f, 1.0f, "dB");
    return l;
}

RadioSimAudioProcessor::RadioSimAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool RadioSimAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == AudioChannelSet::mono() || o == AudioChannelSet::stereo()) && o == l.getMainInputChannelSet();
}

radiosim::UserParams RadioSimAudioProcessor::readParams() const
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    radiosim::UserParams u;
    u.band = (int) g ("band");        u.era = (int) g ("era");        u.region = (int) g ("region");
    u.fmFreqMHz = g ("fmFreq");       u.amFreqKHz = g ("amFreq");     u.distanceKm = g ("distance");
    u.powerKw = g ("power");          u.env = (int) g ("env");        u.antenna = (int) g ("antenna");
    u.ground = (int) g ("ground");    u.timeOfDay = g ("tod");        u.storm = g ("storm");
    u.speedKmh = g ("speed");         u.multipath = g ("mp");         u.adjDb = g ("adj");
    u.adj2Db = g ("adj2");            u.coDb = g ("co");              u.impulse = g ("impulse");
    u.buzz = g ("buzz");              u.detune = g ("detune");        u.stereo = g ("stereo") > 0.5f;
    u.rds = g ("rds") > 0.5f;         u.drive = g ("drive");          u.proc = (int) g ("proc");
    u.speaker = g ("speaker") > 0.5f; u.smart = g ("smart") > 0.5f;   u.trimDb = g ("trim");
    u.mix = g ("mix");                u.outDb = g ("out");
    return u;
}

void RadioSimAudioProcessor::setParam (const char* id, float v)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void RadioSimAudioProcessor::applyPreset (int index)
{
    const auto& list = radiosim::factoryPresets();
    if (index < 0 || index >= (int) list.size()) return;
    const auto& u = list[(size_t) index].p;
    setParam ("band", (float) u.band);        setParam ("era", (float) u.era);          setParam ("region", (float) u.region);
    setParam ("fmFreq", (float) u.fmFreqMHz); setParam ("amFreq", (float) u.amFreqKHz); setParam ("distance", (float) u.distanceKm);
    setParam ("power", (float) u.powerKw);    setParam ("env", (float) u.env);          setParam ("antenna", (float) u.antenna);
    setParam ("ground", (float) u.ground);    setParam ("tod", (float) u.timeOfDay);    setParam ("storm", (float) u.storm);
    setParam ("speed", (float) u.speedKmh);   setParam ("mp", (float) u.multipath);     setParam ("adj", (float) u.adjDb);
    setParam ("adj2", (float) u.adj2Db);      setParam ("co", (float) u.coDb);          setParam ("impulse", (float) u.impulse);
    setParam ("buzz", (float) u.buzz);        setParam ("detune", (float) u.detune);    setParam ("stereo", u.stereo ? 1.f : 0.f);
    setParam ("rds", u.rds ? 1.f : 0.f);      setParam ("drive", (float) u.drive);      setParam ("proc", (float) u.proc);
    setParam ("speaker", u.speaker ? 1.f : 0.f); setParam ("smart", u.smart ? 1.f : 0.f); setParam ("trim", (float) u.trimDb);
    setParam ("mix", (float) u.mix);          setParam ("out", (float) u.outDb);
    apvts.state.setProperty ("preset", index, nullptr);
}

void RadioSimAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate; blockSize = samplesPerBlock;
    engine.prepare (sr, samplesPerBlock);
    engine.setParams (readParams());
    setLatencySamples (engine.latencySamples());
    mono2.setSize (2, samplesPerBlock, false, false, true);
    prepared = true;
}

void RadioSimAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    if (! prepared) return;
    const int n = buffer.getNumSamples(), ch = buffer.getNumChannels();
    engine.setParams (readParams());

    if (ch >= 2)
    {
        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    }
    else if (ch == 1)
    {
        if (mono2.getNumSamples() < n) mono2.setSize (2, n, false, false, true);   // rare: host exceeded its promised block size
        mono2.copyFrom (0, 0, buffer, 0, 0, n);
        mono2.copyFrom (1, 0, buffer, 0, 0, n);
        engine.process (mono2.getWritePointer (0), mono2.getWritePointer (1), n);
        auto* d = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i) d[i] = 0.5f * (mono2.getSample (0, i) + mono2.getSample (1, i));
    }

    const auto& li = engine.linkInfo();
    mField = (float) li.fieldDbuv; mCnr = (float) li.cnrDb; mWave = (float) li.wavelengthM;
    mHorizon = (float) li.horizonKm; mSky = (float) li.skyRatio; mQuarter = (float) li.quarterWaveCm;
    mStereo = (float) engine.meterStereo(); mMute = (float) engine.meterMute();
}

AudioProcessorEditor* RadioSimAudioProcessor::createEditor() { return new RadioSimEditor (*this); }

void RadioSimAudioProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void RadioSimAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RadioSimAudioProcessor(); }
