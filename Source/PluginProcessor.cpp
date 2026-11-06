#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

// ------------------------------------------------------------------ presets
// Starting points, tuned on test signals; adjust by ear on real material.
//                                  depth  thr  sharp att  rel  broad guard max  mix  low     high     trim punch
static const FactoryPreset kPresets[] = {
    { "Default",                  { 100,  2.0, 12,  3,   60,  30,   80,   12,  100, 20,     20000,   0,   50 } },
    { "Vocal - De-Harsh",         { 120,  1.0, 14,  2,   80,  20,   85,   10,  100, 1500,   12000,   0,   30 } },
    { "Vocal - Body & Boxiness",  {  90,  2.0, 10,  5,  100,  40,   80,   10,  100, 150,    1500,    0,   40 } },
    { "Acoustic Guitar",          { 100,  2.0, 12,  3,   70,  30,   90,   12,  100, 200,    8000,    0,   60 } },
    { "Drum Bus - Ring Control",  { 130,  1.0, 20,  1,   40,  10,   40,   14,  100, 150,    6000,    0,   80 } },
    { "Mix Bus - Polish",         {  60,  3.0,  8, 10,  150,  50,   80,    6,  100, 20,     20000,   0,   50 } },
    { "Mastering - Gentle",       {  40,  4.0,  8, 15,  200,  40,   90,    4,  100, 20,     20000,   0,   50 } },
    { "Aggressive",               { 180,  0.0, 16,  1,   50,  30,   60,   18,  100, 20,     20000,   0,   30 } },
};
static const char* kPresetIds[] = { "depth", "sensitivity", "sharpness", "attack", "release", "broad",
                                    "guard", "maxcut", "mix", "low", "high", "trim", "punch" };

int MuziTameProcessor::getNumFactoryPresets() { return (int) std::size (kPresets); }
const FactoryPreset& MuziTameProcessor::getFactoryPreset (int i) { return kPresets[juce::jlimit (0, getNumFactoryPresets() - 1, i)]; }
const juce::String MuziTameProcessor::getProgramName (int i) { return getFactoryPreset (i).name; }

void MuziTameProcessor::loadFactoryPreset (int index)
{
    currentPreset = juce::jlimit (0, getNumFactoryPresets() - 1, index);
    const auto& p = getFactoryPreset (currentPreset);
    for (size_t k = 0; k < std::size (kPresetIds); ++k)
        if (auto* prm = apvts.getParameter (kPresetIds[k]))
            prm->setValueNotifyingHost (prm->convertTo0to1 (p.v[k]));
    apvts.state.setProperty ("preset", currentPreset, nullptr);
}

// ------------------------------------------------------------------ parameters
APVTS::ParameterLayout MuziTameProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withLabel ("%");
    auto db  = AudioParameterFloatAttributes().withLabel ("dB");
    auto ms  = AudioParameterFloatAttributes().withLabel ("ms");
    auto hz  = AudioParameterFloatAttributes().withLabel ("Hz")
                 .withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; });
    auto freqRange = [] (float lo, float hi)
    {
        return NormalisableRange<float> (lo, hi,
            [] (float a, float b, float t) { return a * std::pow (b / a, t); },
            [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
            [] (float, float, float v) { return v; });
    };

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "depth", 1 }, "Depth", NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sensitivity", 1 }, "Threshold", NormalisableRange<float> (-6.0f, 12.0f, 0.1f), 2.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sharpness", 1 }, "Sharpness", NormalisableRange<float> (4.0f, 40.0f, 0.1f, 0.5f), 12.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attack", NormalisableRange<float> (0.5f, 50.0f, 0.1f, 0.4f), 3.0f, ms));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release", NormalisableRange<float> (10.0f, 500.0f, 1.0f, 0.4f), 60.0f, ms));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "broad", 1 }, "Broad", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "guard", 1 }, "Tonal Guard", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 80.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "maxcut", 1 }, "Max Cut", NormalisableRange<float> (3.0f, 24.0f, 0.1f), 12.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "delta", 1 }, "Delta", false));
    l.add (std::make_unique<AudioParameterChoice>(ParameterID { "mode", 1 }, "Mode", StringArray { "Standard", "High-res", "Live" }, 2));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "low", 2 }, "Low Limit", freqRange (20.0f, 20000.0f), 20.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "high", 2 }, "High Limit", freqRange (20.0f, 20000.0f), 20000.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "trim", 2 }, "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "bypass", 2 }, "Bypass", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "punch", 3 }, "Punch", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct));
    return l;
}

MuziTameProcessor::MuziTameProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MuziTame", createLayout())
{
    depth       = apvts.getRawParameterValue ("depth");
    sensitivity = apvts.getRawParameterValue ("sensitivity");
    sharpness   = apvts.getRawParameterValue ("sharpness");
    attack      = apvts.getRawParameterValue ("attack");
    release     = apvts.getRawParameterValue ("release");
    broad       = apvts.getRawParameterValue ("broad");
    guard       = apvts.getRawParameterValue ("guard");
    maxCut      = apvts.getRawParameterValue ("maxcut");
    mix         = apvts.getRawParameterValue ("mix");
    delta       = apvts.getRawParameterValue ("delta");
    mode        = apvts.getRawParameterValue ("mode");
    lowHz       = apvts.getRawParameterValue ("low");
    highHz      = apvts.getRawParameterValue ("high");
    trim        = apvts.getRawParameterValue ("trim");
    bypass      = apvts.getRawParameterValue ("bypass");
    punch       = apvts.getRawParameterValue ("punch");
    bypassParam = apvts.getParameter ("bypass");
}

bool MuziTameProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void MuziTameProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    const int ch = juce::jmax (1, getTotalNumOutputChannels());
    engines[0].prepare (sampleRate, ch, 0);        // allocation happens here, never in processBlock
    engines[1].prepare (sampleRate, ch, 1);
    engines[2].prepare (sampleRate, ch, 2);        // Live: zero latency, for tracking and live use
    const int m = (int) mode->load();
    activeMode.store (m);
    setLatencySamples (engines[(size_t) m].getLatencySamples());
    trimGain.reset (sampleRate, 0.05);
    trimGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (trim->load()));
    monoScratch.assign ((size_t) juce::jmax (samplesPerBlock, 8192), 0.0f);
}

void MuziTameProcessor::pushTap (AudioTap& tap, const juce::AudioBuffer<float>& b, std::atomic<float>& level)
{
    const int n = juce::jmin (b.getNumSamples(), (int) monoScratch.size());
    const int ch = b.getNumChannels();
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < ch; ++c) { const float v = b.getSample (c, i); s += v; peak = juce::jmax (peak, std::abs (v)); }
        monoScratch[(size_t) i] = s / (float) juce::jmax (1, ch);
    }
    tap.push (monoScratch.data(), n);
    const float decay = std::exp (-(float) n / (float) (currentRate * 0.3));     // 300 ms fall
    level.store (juce::jmax (peak, level.load() * decay));
}

void MuziTameProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    pushTap (preTap, buffer, inLevel);

    const int m = (int) mode->load();
    if (m != activeMode.load())
    {
        engines[(size_t) m].reset();               // no allocation: all were prepared
        activeMode.store (m);
        setLatencySamples (engines[(size_t) m].getLatencySamples());
    }

    const bool bypassed = bypass->load() > 0.5f;
    muzi::FrankenParams p;
    p.depth         = depth->load() / 100.0f;
    p.sensitivityDb = sensitivity->load();
    p.sharpness     = sharpness->load();
    p.attackMs      = attack->load();
    p.releaseMs     = release->load();
    p.broadDepth    = 0.3f * broad->load() / 30.0f;   // 30 % = the Lab default
    p.tonalGuard    = guard->load() / 100.0f;
    p.transientGuard = punch->load() / 100.0f;
    p.maxCutDb      = maxCut->load();
    p.mix           = bypassed ? 0.0f : mix->load() / 100.0f;   // bypass keeps the latency
    p.delta         = ! bypassed && delta->load() > 0.5f;
    p.lowHz         = juce::jmin (lowHz->load(), highHz->load());
    p.highHz        = juce::jmax (lowHz->load(), highHz->load());

    auto& eng = engines[(size_t) m];
    eng.setParams (p);
    eng.process (buffer.getArrayOfWritePointers(), buffer.getNumSamples());

    trimGain.setTargetValue (bypassed ? 1.0f : juce::Decibels::decibelsToGain (trim->load()));
    if (trimGain.isSmoothing() || std::abs (trimGain.getTargetValue() - 1.0f) > 1e-6f)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float g = trimGain.getNextValue();
            for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.getWritePointer (c)[i] *= g;
        }
    }

    pushTap (postTap, buffer, outLevel);
}

juce::AudioProcessorEditor* MuziTameProcessor::createEditor() { return new MuziTameEditor (*this); }

void MuziTameProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    apvts.state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MuziTameProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset = apvts.state.getProperty ("preset", 0);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MuziTameProcessor(); }
