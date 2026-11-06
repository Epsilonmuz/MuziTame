// Renders the editor to PNG after feeding the plugin a test signal, so the GUI
// can be checked without a display:  MuziSnapshot <signal.wav> <out.png>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    MuziTameProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.setNonRealtime (true);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioFormatManager afm; afm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (afm.createReaderFor (juce::File (juce::String (argv[1]))));
    const int n = (int) std::min<juce::int64> (rd->lengthInSamples, 48000 * 3);
    if (argc > 4) proc.loadFactoryPreset (juce::String (argv[4]).getIntValue());
    juce::AudioBuffer<float> buf (2, n); rd->read (&buf, 0, n, 0, true, true);
    juce::MidiBuffer midi;
    for (int s = 0; s + 512 <= n; s += 512) { juce::AudioBuffer<float> v (buf.getArrayOfWritePointers(), 2, s, 512); proc.processBlock (v, midi); }
    std::unique_ptr<MuziTameEditor> ed (static_cast<MuziTameEditor*> (proc.createEditor()));
    if (argc > 3) ed->setSize (juce::String (argv[3]).getIntValue(), juce::String (argv[3]).getIntValue() * 600 / 960);
    ed->refreshForSnapshot();
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File out { juce::String (argv[2]) }; out.deleteFile();
    juce::FileOutputStream os (out); juce::PNGImageFormat().writeImageToStream (img, os);
    return 0;
}
