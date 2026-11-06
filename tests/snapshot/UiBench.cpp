// Times the editor's per-frame work (analyser update + full repaint at Retina scale)
// with JUCE's software renderer:  MuziUiBench
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <chrono>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    MuziTameProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    std::unique_ptr<MuziTameEditor> ed (static_cast<MuziTameEditor*> (proc.createEditor()));
    juce::Image img (juce::Image::ARGB, ed->getWidth() * 2, ed->getHeight() * 2, true, juce::SoftwareImageType());
    double tUpdate = 0, tPaint = 0, tDisp = 0; int frames = 0; double phase = 0;
    auto& disp = ed->benchDisplay();
    juce::Image dimg (juce::Image::ARGB, disp.getWidth() * 2, disp.getHeight() * 2, true, juce::SoftwareImageType());
    for (int f = 0; f < 150; ++f)
    {
        for (int b = 0; b < 3; ++b)            // ~1/30 s of audio per frame
        {
            for (int i = 0; i < 512; ++i, phase += 1.0 / 48000.0)
            {
                double s = 0; for (int h = 1; h < 10; ++h) s += std::sin (2 * juce::MathConstants<double>::pi * 110 * h * phase) / h;
                s += 0.3 * std::sin (2 * juce::MathConstants<double>::pi * 2750 * phase);
                buf.setSample (0, i, (float) (0.2 * s)); buf.setSample (1, i, (float) (0.2 * s));
            }
            proc.processBlock (buf, midi);
        }
        auto t0 = std::chrono::steady_clock::now();
        ed->benchFrame();
        auto t1 = std::chrono::steady_clock::now();
        { juce::Graphics g (img); g.addTransform (juce::AffineTransform::scale (2.0f)); ed->paintEntireComponent (g, true); }
        auto t2 = std::chrono::steady_clock::now();
        { juce::Graphics g (dimg); g.addTransform (juce::AffineTransform::scale (2.0f)); disp.paintEntireComponent (g, true); }
        auto t3 = std::chrono::steady_clock::now();
        if (f >= 30) tDisp += std::chrono::duration<double, std::milli> (t3 - t2).count();
        if (f >= 30) { tUpdate += std::chrono::duration<double, std::milli> (t1 - t0).count(); tPaint += std::chrono::duration<double, std::milli> (t2 - t1).count(); ++frames; }
    }
    std::printf ("per frame: update %.2f ms, analyser repaint @2x %.2f ms (whole window %.2f ms)  ->  about %.0f %% of one core at 30 fps\n",
                 tUpdate / frames, tDisp / frames, tPaint / frames, (tUpdate + tDisp) / frames * 30 / 10);
    return 0;
}
