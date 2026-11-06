// User preset round trip, without a display:  MuziPresetTest
// Saves, lists, reloads and deletes presets in a temporary folder (MUZI_PRESET_DIR), and checks
// the "modified" mark, session save/restore, and that Mode/Delta/Bypass are never touched.
#include "PluginProcessor.h"
#include "PluginEditor.h"

static int failures = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static float get (MuziTameProcessor& p, const char* id) { auto* r = p.apvts.getParameter (id); return r->convertFrom0to1 (r->getValue()); }
static void set (MuziTameProcessor& p, const char* id, float v) { auto* r = p.apvts.getParameter (id); r->setValueNotifyingHost (r->convertTo0to1 (v)); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("muzi_preset_test");
    dir.deleteRecursively();
    setenv ("MUZI_PRESET_DIR", dir.getFullPathName().toRawUTF8(), 1);

    MuziTameProcessor p;
    CHECK (MuziTameProcessor::getUserPresets().isEmpty());
    CHECK (! p.presetModified());

    // factory 808 preset is the last one, and loads its values
    const int n808 = MuziTameProcessor::getNumFactoryPresets() - 1;
    CHECK (juce::String (MuziTameProcessor::getFactoryPreset (n808).name).startsWith ("808"));
    p.loadFactoryPreset (n808);
    CHECK (std::abs (get (p, "depth") - 140.0f) < 0.2f);
    CHECK (std::abs (get (p, "high") - 5000.0f) < 5.0f);
    CHECK (! p.presetModified());

    // tweak, save as a user preset
    set (p, "depth", 87.0f); set (p, "guard", 33.0f);
    CHECK (p.presetModified());
    set (p, "mode", 0.0f); set (p, "delta", 1.0f);
    const auto f = p.saveUserPreset ("808Demudder");
    CHECK (f.existsAsFile());
    CHECK (p.currentPreset == -1 && p.userPresetName == "808Demudder");
    CHECK (! p.presetModified());
    CHECK (MuziTameProcessor::getUserPresets().size() == 1);

    // illegal file name characters are cleaned, not fatal
    CHECK (p.saveUserPreset ("a/b:c").existsAsFile());
    CHECK (p.saveUserPreset ("   ") == juce::File());
    CHECK (MuziTameProcessor::getUserPresets().size() == 2);

    // load factory, then the user preset again: values come back, Mode/Delta untouched
    p.loadFactoryPreset (0);
    CHECK (std::abs (get (p, "depth") - 100.0f) < 0.2f);
    set (p, "mode", 2.0f); set (p, "delta", 0.0f);
    CHECK (p.loadUserPreset (f));
    CHECK (std::abs (get (p, "depth") - 87.0f) < 0.2f);
    CHECK (std::abs (get (p, "guard") - 33.0f) < 0.2f);
    CHECK (std::abs (get (p, "high") - 5000.0f) < 5.0f);
    CHECK (juce::roundToInt (get (p, "mode")) == 2);        // still Live
    CHECK (get (p, "delta") < 0.5f);
    CHECK (p.currentPresetName() == "808Demudder");

    // session save / restore keeps the user preset name, and counts as unmodified
    set (p, "depth", 95.0f);
    juce::MemoryBlock state; p.getStateInformation (state);
    MuziTameProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.currentPreset == -1 && q.userPresetName == "808Demudder");
    CHECK (std::abs (get (q, "depth") - 95.0f) < 0.2f);
    CHECK (! q.presetModified());
    CHECK (q.getCurrentProgram() == 0);                       // hosts never see -1

    // an old (v0.7) session with a factory preset still restores it
    q.loadFactoryPreset (3);
    juce::MemoryBlock st2; q.getStateInformation (st2);
    MuziTameProcessor r; r.setStateInformation (st2.getData(), (int) st2.getSize());
    CHECK (r.currentPreset == 3 && r.userPresetName.isEmpty());

    // a broken file is refused without touching anything
    auto bad = dir.getChildFile ("broken.mtpreset"); bad.replaceWithText ("not xml");
    const float before = get (p, "depth");
    CHECK (! p.loadUserPreset (bad));
    CHECK (get (p, "depth") == before);
    bad.deleteFile();

    // editor: opens with presets on disk, shows the name, steps through factory + user
    {
        p.loadUserPreset (f);
        std::unique_ptr<MuziTameEditor> ed (static_cast<MuziTameEditor*> (p.createEditor()));
        auto* box = dynamic_cast<juce::ComboBox*> (ed->findPart ("presetBox"));
        CHECK (box != nullptr && box->getText() == "808Demudder");
        set (p, "depth", 120.0f);
        for (int i = 0; i < 10; ++i) ed->benchFrame();
        CHECK (box->getText() == "808Demudder *");
        dynamic_cast<juce::Button*> (ed->findPart ("next"))->onClick();
        CHECK (p.currentPreset == -1);                          // next user preset after 808Demudder
        dynamic_cast<juce::Button*> (ed->findPart ("next"))->onClick();
        CHECK (p.currentPreset == 0);                           // wraps to the first factory preset
        dynamic_cast<juce::Button*> (ed->findPart ("prev"))->onClick();
        CHECK (p.currentPreset == -1);                          // back to the last user preset
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)   // dialogs need a screen
        {
            ed->showSaveDialogForSnapshot ("Test");
            CHECK (ed->dialogForSnapshot() != nullptr);
        }
    }

    dir.deleteRecursively();
    std::printf (failures == 0 ? "PRESET TEST PASSED\n" : "PRESET TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
