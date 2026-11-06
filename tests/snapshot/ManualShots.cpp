// Renders the pictures used in the manual, straight from the real editor:
//   MuziManualShots <signal.wav> <out_dir> [factory preset index]
// Writes full.png (whole window, 2x) and one close-up per control: <part>.png,
// plus parts.json with every control's position, for the numbered overview.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>

static void save (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::puts ("usage: MuziManualShots signal.wav out_dir [preset]"); return 2; }
    MuziTameProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.setNonRealtime (true);
    if (argc > 3) proc.loadFactoryPreset (juce::String (argv[3]).getIntValue());
    proc.apvts.getParameter ("mode")->setValueNotifyingHost (proc.apvts.getParameter ("mode")->convertTo0to1 (2.0f));   // Live, the default
    proc.prepareToPlay (48000.0, 512);

    juce::AudioFormatManager afm; afm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (afm.createReaderFor (juce::File (juce::String (argv[1]))));
    const int n = (int) std::min<juce::int64> (rd->lengthInSamples, 48000 * 4);
    juce::AudioBuffer<float> buf (2, n); rd->read (&buf, 0, n, 0, true, true);
    juce::MidiBuffer midi;
    for (int s = 0; s + 512 <= n; s += 512) { juce::AudioBuffer<float> v (buf.getArrayOfWritePointers(), 2, s, 512); proc.processBlock (v, midi); }

    const float scale = 2.0f;
    std::unique_ptr<MuziTameEditor> ed (static_cast<MuziTameEditor*> (proc.createEditor()));
    ed->refreshForSnapshot();
    for (int i = 0; i < 4; ++i) ed->benchFrame();
    const juce::File out { juce::String (argv[2]) };
    out.createDirectory();
    const auto full = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
    save (full, out.getChildFile ("full.png"));

    // close-ups: each part's bounds, padded, cut from the full picture
    auto crop = [&] (juce::Rectangle<int> r, int pad, const juce::String& name)
    {
        r = r.expanded (pad).getIntersection (ed->getLocalBounds());
        const auto px = (r.toFloat() * scale).toNearestInt();
        save (full.getClippedImage (px), out.getChildFile (name + ".png"));
    };
    auto boundsOf = [&] (const char* id) { return ed->findPart (id)->getBoundsInParent(); };

    juce::DynamicObject::Ptr json = new juce::DynamicObject();
    for (auto* id : { "attack", "release", "punch", "threshold", "depth", "sharpness", "broad", "guard", "maxcut", "mix", "trim",
                      "presetBox", "prev", "next", "save", "live", "standard", "hires", "delta", "bypass", "analyser", "peakguard", "display" })
    {
        const auto b = boundsOf (id);
        juce::Array<juce::var> a { b.getX(), b.getY(), b.getWidth(), b.getHeight() };
        json->setProperty (id, a);
    }
    const auto meters = boundsOf ("inMeter").getUnion (boundsOf ("outMeter"));
    json->setProperty ("meters", juce::Array<juce::var> { meters.getX(), meters.getY(), meters.getWidth(), meters.getHeight() });
    json->setProperty ("width", ed->getWidth());
    json->setProperty ("height", ed->getHeight());
    out.getChildFile ("parts.json").replaceWithText (juce::JSON::toString (juce::var (json.get())));

    for (auto* id : { "attack", "release", "punch", "threshold", "depth", "sharpness", "broad", "guard", "maxcut", "mix", "trim" })
        crop (boundsOf (id), 0, id);
    crop (meters.withTrimmedBottom (-4), 6, "meters");
    crop (boundsOf ("prev").getUnion (boundsOf ("save")), 6, "presetbar");
    crop (boundsOf ("live").getUnion (boundsOf ("hires")), 6, "modes");
    crop (boundsOf ("save"), 6, "save");
    crop (boundsOf ("delta"), 6, "delta");
    crop (boundsOf ("bypass"), 6, "bypass");
    crop (boundsOf ("analyser"), 4, "analyser");
    crop (boundsOf ("peakguard"), 4, "peakguard");
    crop (boundsOf ("display"), 0, "display");
    const auto d = boundsOf ("display");
    crop (d.withWidth (d.getWidth() / 3), 0, "range_low");
    crop (d.withTrimmedLeft (d.getWidth() * 2 / 3), 0, "range_high");
    // the four panels
    const auto knobsTop = boundsOf ("attack").getY() - 40;
    crop (juce::Rectangle<int>::leftTopRightBottom (16, knobsTop, 246, 570), 0, "panel_dynamics");
    crop (juce::Rectangle<int>::leftTopRightBottom (254, knobsTop, 574, 570), 0, "panel_tame");
    crop (juce::Rectangle<int>::leftTopRightBottom (582, knobsTop, 814, 570), 0, "panel_character");
    crop (juce::Rectangle<int>::leftTopRightBottom (822, knobsTop, 944, 570), 0, "panel_output");

    // Delta switched on, and the knobs moved after loading a preset (the * mark)
    proc.apvts.getParameter ("delta")->setValueNotifyingHost (1.0f);
    proc.apvts.getParameter ("depth")->setValueNotifyingHost (0.6f);
    ed->refreshForSnapshot();
    for (int i = 0; i < 20; ++i) ed->benchFrame();
    const auto full2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
    save (full2.getClippedImage ((boundsOf ("delta").expanded (6).toFloat() * scale).toNearestInt()), out.getChildFile ("delta_on.png"));
    save (full2.getClippedImage ((boundsOf ("prev").getUnion (boundsOf ("save")).expanded (6).toFloat() * scale).toNearestInt()),
          out.getChildFile ("presetbar_modified.png"));
    proc.apvts.getParameter ("delta")->setValueNotifyingHost (0.0f);
    proc.apvts.getParameter ("bypass")->setValueNotifyingHost (1.0f);
    ed->refreshForSnapshot();
    const auto full3 = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
    save (full3.getClippedImage ((boundsOf ("bypass").expanded (6).toFloat() * scale).toNearestInt()), out.getChildFile ("bypass_on.png"));
    std::puts ("shots written");
    return 0;
}
