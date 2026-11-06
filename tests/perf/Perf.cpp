// Muzi Tame performance report for a real Mac (run by .github/workflows/perf.yml).
//  1. Audio: every mode, sample rate and host buffer size. Each processBlock call is timed
//     against its real-time budget (buffer / sample rate). Over 100 % means a dropout.
//  2. Display: the editor's per-frame work, drawn with the platform renderer (CoreGraphics on
//     macOS) at Retina scale, for each analyser setting.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <chrono>
#include <cstdio>

namespace
{
using Clock = std::chrono::steady_clock;

std::vector<float> musicLike (double fs, int n, int channel)
{
    // bass + chord partials + a fixed resonance + hats-like noise bursts, changing every half second
    std::vector<float> x ((size_t) n);
    uint32_t seed = 12345u + (uint32_t) channel;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, beat = std::fmod (t, 0.5), env = std::exp (-5.0 * beat);
        const double f0 = 110.0 * std::pow (2.0, std::floor (t * 2.0) * 3.0 / 12.0 - 12.0 * std::floor (t / 2.0) / 12.0);
        double s = 0.0;
        for (int h = 1; h < 14; ++h) s += std::sin (2.0 * juce::MathConstants<double>::pi * f0 * h * t) / h;
        s += 0.35 * std::sin (2.0 * juce::MathConstants<double>::pi * 2750.0 * t);
        seed = seed * 1664525u + 1013904223u;
        const double noise = ((seed >> 9) / 8388608.0 - 1.0) * std::exp (-60.0 * std::fmod (t, 0.125));
        x[(size_t) i] = (float) (0.18 * env * s + 0.05 * noise);
    }
    return x;
}

struct Result { double avg, p99, p999, worst; int over50, over100, blocks; };

Result runAudio (double fs, int mode, int block)
{
    MuziTameProcessor proc;
    auto* modeParam = proc.apvts.getParameter ("mode");
    modeParam->setValueNotifyingHost (modeParam->convertTo0to1 ((float) mode));
    proc.setPlayConfigDetails (2, 2, fs, block);
    proc.prepareToPlay (fs, block);
    const int seconds = 12, n = (int) (fs * seconds);
    const auto L = musicLike (fs, n, 0), R = musicLike (fs, n, 1);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    std::vector<double> pct;
    const double budget = block / fs;
    for (int pos = 0; pos + block <= n; pos += block)
    {
        buf.copyFrom (0, 0, L.data() + pos, block);
        buf.copyFrom (1, 0, R.data() + pos, block);
        const auto t0 = Clock::now();
        proc.processBlock (buf, midi);
        const double dt = std::chrono::duration<double> (Clock::now() - t0).count();
        if (pos >= (int) fs) pct.push_back (100.0 * dt / budget);      // skip the first second (warm-up)
    }
    Result r {};
    r.blocks = (int) pct.size();
    double sum = 0; for (double p : pct) { sum += p; r.over50 += p > 50.0; r.over100 += p > 100.0; }
    r.avg = sum / (double) pct.size();
    std::sort (pct.begin(), pct.end());
    r.p99 = pct[(size_t) (pct.size() * 0.99)];
    r.p999 = pct[(size_t) (pct.size() * 0.999)];
    r.worst = pct.back();
    proc.releaseResources();
    return r;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const char* modeName[] = { "Standard", "High-res", "Live" };
    std::printf ("## Audio: share of each block's real-time budget (over 100 %% = dropout)\n\n");
    std::printf ("| Rate | Mode | Buffer | Average | 99th pct | 99.9th pct | Worst | Blocks over 50 %% / 100 %% |\n|---|---|---|---|---|---|---|---|\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int mode = 0; mode < 3; ++mode)
            for (int block : { 64, 128, 256, 512 })
            {
                if (fs > 50000.0 && (block == 64 || block == 256)) continue;
                const auto r = runAudio (fs, mode, block);
                std::printf ("| %.1f kHz | %s | %d | %.1f %% | %.0f %% | %.0f %% | %.0f %% | %d / %d of %d |\n",
                             fs / 1000.0, modeName[mode], block, r.avg, r.p99, r.p999, r.worst, r.over50, r.over100, r.blocks);
                std::fflush (stdout);
            }

    // ---------------------------------------------------------------- display
    MuziTameProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    const auto L = musicLike (48000.0, 48000 * 4, 0);
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    std::unique_ptr<MuziTameEditor> ed (static_cast<MuziTameEditor*> (proc.createEditor()));
    auto& disp = ed->benchDisplay();
    std::printf ("\n## Display: main-thread work at Retina (2x), drawn with the native renderer\n\n");
    std::printf ("| Analyser | Update per frame | Draw per frame | Total per second | Share of one core |\n|---|---|---|---|---|\n");
    const char* qName[] = { "Full", "Light", "Off" };
    int pos = 0;
    // one window-sized native bitmap reused for every frame, like a window's backing store
    juce::Image img (juce::Image::ARGB, disp.getWidth() * 2, disp.getHeight() * 2, true, juce::NativeImageType());
    for (int q = 0; q < 3; ++q)
    {
        ed->benchSetAnalyser (q);
        const int every = q == 0 ? 1 : q == 1 ? 2 : 3;
        double tUpd = 0, tDraw = 0; int draws = 0;
        const int ticks = 150;                                   // 5 s of 30 Hz timer ticks
        for (int f = 0; f < ticks; ++f)
        {
            for (int b = 0; b < 3; ++b)                          // ~1/30 s of audio per tick
            {
                buf.copyFrom (0, 0, L.data() + pos, 512); buf.copyFrom (1, 0, L.data() + pos, 512);
                pos = (pos + 512) % (48000 * 4 - 512);
                proc.processBlock (buf, midi);
            }
            const auto t0 = Clock::now();
            ed->benchFrame();
            const auto t1 = Clock::now();
            tUpd += std::chrono::duration<double, std::milli> (t1 - t0).count();
            if (f % every == 0)
            {
                const auto t2 = Clock::now();
                { juce::Graphics g (img); g.addTransform (juce::AffineTransform::scale (2.0f)); disp.paintEntireComponent (g, true); }
                tDraw += std::chrono::duration<double, std::milli> (Clock::now() - t2).count();
                ++draws;
            }
        }
        const double perSecond = (tUpd + tDraw) / (ticks / 30.0);
        std::printf ("| %s | %.2f ms | %.2f ms | %.0f ms | %.1f %% |\n", qName[q], tUpd / ticks, draws ? tDraw / draws : 0.0, perSecond, perSecond / 10.0);
    }
    return 0;
}
