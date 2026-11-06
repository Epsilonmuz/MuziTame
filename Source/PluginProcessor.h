#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FrankenTameEngine.h"

// Single-producer / single-consumer audio tap for the analyser (audio thread
// writes, GUI thread reads). Never blocks or allocates on the audio thread.
struct AudioTap
{
    static constexpr int kSize = 1 << 15;
    juce::AbstractFifo fifo { kSize };
    std::vector<float> data = std::vector<float> (kSize, 0.0f);

    void push (const float* x, int n)
    {
        const auto scope = fifo.write (n);       // drops samples if the GUI falls behind
        if (scope.blockSize1 > 0) std::copy (x, x + scope.blockSize1, data.begin() + scope.startIndex1);
        if (scope.blockSize2 > 0) std::copy (x + scope.blockSize1, x + scope.blockSize1 + scope.blockSize2, data.begin() + scope.startIndex2);
    }
    int pull (float* dst, int maxN)
    {
        const auto scope = fifo.read (juce::jmin (maxN, fifo.getNumReady()));
        if (scope.blockSize1 > 0) std::copy (data.begin() + scope.startIndex1, data.begin() + scope.startIndex1 + scope.blockSize1, dst);
        if (scope.blockSize2 > 0) std::copy (data.begin() + scope.startIndex2, data.begin() + scope.startIndex2 + scope.blockSize2, dst + scope.blockSize1);
        return scope.blockSize1 + scope.blockSize2;
    }
};

struct FactoryPreset
{
    const char* name;
    // depth, threshold, sharpness, attack, release, broad, guard, maxcut, mix, low, high, trim, punch
    float v[13];
};

class MuziTameProcessor : public juce::AudioProcessor
{
public:
    MuziTameProcessor();
    ~MuziTameProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Muzi Tame"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return getNumFactoryPresets(); }
    int getCurrentProgram() override { return juce::jmax (0, currentPreset); }
    void setCurrentProgram (int index) override { loadFactoryPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // presets
    static int getNumFactoryPresets();
    static const FactoryPreset& getFactoryPreset (int index);
    void loadFactoryPreset (int index);
    int currentPreset = 0;                          // factory index, or -1 while a user preset is loaded

    // user presets: .mtpreset files in a folder of their own (message thread only)
    static constexpr const char* kUserPresetExt = ".mtpreset";
    static juce::File getUserPresetFolder();
    static juce::Array<juce::File> getUserPresets();
    juce::File saveUserPreset (const juce::String& name);    // returns the file, or {} on failure
    bool loadUserPreset (const juce::File& file);
    juce::String userPresetName;                     // name of the loaded user preset, empty for factory
    juce::String currentPresetName() const;
    bool presetModified() const;                     // knobs moved since the preset was loaded
    std::atomic<int> presetVersion { 0 };            // bumps whenever the loaded preset changes

    // GUI access
    const muzi::FrankenTameEngine& activeEngine() const { return engines[(size_t) activeMode.load()]; }
    double getCurrentSampleRate() const { return currentRate; }
    float getSharpness() const { return sharpness->load(); }
    AudioTap preTap, postTap;
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f };   // peak, linear, with decay

private:
    std::array<muzi::FrankenTameEngine, 3> engines;   // Standard, High-res, Live

    // Runs the spectral analysis next to the audio thread, so each audio block only pays for
    // the filters: a flat, small cost per block at any buffer size (no spikes, no dropouts).
    struct AnalysisWorker : juce::Thread
    {
        explicit AnalysisWorker (MuziTameProcessor& o) : juce::Thread ("Muzi Tame analysis"), owner (o) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                int hops = 0;
                for (auto& e : owner.engines) hops += e.serviceAnalysis();
                if (hops == 0) wait (1);          // new audio arrives every few ms
            }
        }
        MuziTameProcessor& owner;
    };
    AnalysisWorker worker { *this };
    void startWorker();
    void stopWorker();
    std::atomic<int> activeMode { 0 };
    double currentRate = 48000.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> trimGain;
    std::vector<float> monoScratch;

    std::atomic<float>* depth = nullptr;
    std::atomic<float>* sensitivity = nullptr;
    std::atomic<float>* sharpness = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* broad = nullptr;
    std::atomic<float>* guard = nullptr;
    std::atomic<float>* maxCut = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* delta = nullptr;
    std::atomic<float>* mode = nullptr;
    std::atomic<float>* lowHz = nullptr;
    std::atomic<float>* highHz = nullptr;
    std::atomic<float>* trim = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::atomic<float>* punch = nullptr;
    std::atomic<float>* peakGuard = nullptr;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    void pushTap (AudioTap& tap, const juce::AudioBuffer<float>& b, std::atomic<float>& level);
    float presetSnapshot[13] {};                     // values as loaded, for the "modified" mark
    void takePresetSnapshot();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuziTameProcessor)
};
