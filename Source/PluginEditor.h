#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "MuziLookAndFeel.h"

// ---------------------------------------------------------------- knob
// Rotary slider + name above + live value below. Double-click resets.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& name,
          const juce::String& tooltip, bool bipolar = false);
    void resized() override;
    void paint (juce::Graphics&) override;
    float textScale = 1.0f;
    bool hero = false;
    juce::Slider slider;

private:
    juce::String name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// ---------------------------------------------------------------- bypass
class PowerButton : public juce::Button
{
public:
    PowerButton() : juce::Button ("Bypass") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// ---------------------------------------------------------------- meters
class LevelMeter : public juce::Component
{
public:
    LevelMeter (std::atomic<float>& src, const juce::String& text) : source (src), label (text) {}
    void tick();
    void paint (juce::Graphics&) override;
    float textScale = 1.0f;

private:
    std::atomic<float>& source;
    juce::String label;
    float shown = -60.0f, hold = -60.0f;
    int holdTicks = 0;
};

// ---------------------------------------------------------------- analyser + cut curve + range handles
class AnalyzerDisplay : public juce::Component
{
public:
    explicit AnalyzerDisplay (MuziTameProcessor&);
    int editorHeight = 600;                         // to continue the editor's background gradient (opaque)
    int quality = 0;                                // analyser: 0 full, 1 light (15 fps), 2 off (cut curve only)
    void update();                                  // called from the editor's timer (and snapshot tool)
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    float textScale = 1.0f;

private:
    static constexpr int kOrder = 12, kN = 1 << kOrder;
    MuziTameProcessor& proc;
    juce::dsp::FFT fft { kOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kN, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> ringIn = std::vector<float> (kN, 0.0f), ringOut = std::vector<float> (kN, 0.0f);
    int ringPos = 0;
    std::vector<float> work = std::vector<float> (kN * 2, 0.0f), scratch = std::vector<float> (AudioTap::kSize, 0.0f);
    std::vector<float> specIn = std::vector<float> (kN / 2 + 1, -120.0f), specOut = std::vector<float> (kN / 2 + 1, -120.0f);
    std::vector<float> colIn, colOut, colCut, colBroad;     // one value per display column
    float nGainN[muzi::FrankenTameEngine::kNarrow] {}, nFreqN[muzi::FrankenTameEngine::kNarrow] {};
    int hoverX = -1, dragging = 0;                  // dragging: 1 = low, 2 = high
    // performance: static layer cached as an image, per-column cos(w)/cos(2w) for the curve maths,
    // and no work at all while the plugin is idle
    juce::Image staticLayer, frame;
    void paintContent (juce::Graphics&, float pixelScale);
    float staticScale = 0.0f;
    void renderStaticLayer (float pixelScale);
    std::vector<double> cosW, cos2W;
    double cosRate = 0.0;
    int idleFrames = 0;
    juce::String lastFooter;
    friend class MuziTameEditor;

    juce::Rectangle<float> plot() const;
    float xForFreq (double f) const;
    double freqForX (float x) const;
    float ySpec (float db) const;
    float yCut (float db) const;
    void computeSpectrum (std::vector<float>& ring, std::vector<float>& spec);
    void toColumns (const std::vector<float>& spec, std::vector<float>& cols);
    juce::RangedAudioParameter* param (int which) const;
    float limitHz (int which) const;
    int handleAt (float x) const;
};

// ---------------------------------------------------------------- preset menu
// A ComboBox that re-reads the user preset folder every time it opens, so presets
// saved in another instance (or copied into the folder by hand) show up straight away.
class PresetBox : public juce::ComboBox
{
public:
    std::function<void()> onAboutToOpen;
    void showPopup() override { if (onAboutToOpen) onAboutToOpen(); juce::ComboBox::showPopup(); }
};

// ---------------------------------------------------------------- editor
class MuziTameEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MuziTameEditor (MuziTameProcessor&);
    ~MuziTameEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshForSnapshot();
    void benchFrame() { timerCallback(); }        // one GUI frame, for the CPU benchmark
    juce::Component& benchDisplay() { return display; }
    void benchSetAnalyser (int q) { setAnalyserQuality (q); }
    std::vector<juce::Component*> animatedParts() { return { &display, &inMeter, &outMeter }; }

private:
    void timerCallback() override;
    void refreshPresetBox();
    juce::Rectangle<int> sc (float x, float y, float w, float h) const;

    MuziTameProcessor& proc;
    muzi::ui::MuziLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };
    float s = 1.0f;

    AnalyzerDisplay display;
    Knob depth, threshold, sharpness, attack, release, punch, broad, guard, maxcut, mix, trim;
    LevelMeter inMeter, outMeter;

    PresetBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, savePreset { "SAVE" };
    juce::Array<juce::File> userFiles;               // as listed in the menu
    int shownPresetVersion = -1;
    juce::String shownPresetText;
    std::unique_ptr<juce::AlertWindow> dialog;
    enum { kUserBase = 1000, kSaveId = 9001, kDeleteId = 9002, kFolderId = 9003 };
    void rebuildPresetMenu();
    void presetChosen (int id);
    void stepPreset (int delta);
    void showSaveDialog();
    void confirmDelete();
    void showDialog (std::unique_ptr<juce::AlertWindow>, std::function<void (int)> onResult);

public:
    // for the manual's screenshots and tests
    juce::Component* findPart (const juce::String& id);
    void showSaveDialogForSnapshot (const juce::String& name) { showSaveDialog(); if (dialog) dialog->getTextEditor ("name")->setText (name); }
    juce::Component* dialogForSnapshot() { return dialog.get(); }
private:
    juce::TextButton modeLive { "LIVE" }, modeStd { "STANDARD" }, modeHi { "HI-RES" }, deltaBtn { "DELTA" }, analyserBtn;
    int frameCount = 0;
    void setAnalyserQuality (int q);
    PowerButton bypassBtn;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAttachment, bypassAttachment;

    juce::Rectangle<int> panelDyn, panelTame, panelChar, panelOut, header, footer;
    juce::String footerText() const;
    juce::String shownFooter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuziTameEditor)
};
