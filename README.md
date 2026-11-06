# Muzi Tame v0.7.0 — FrankenTame engine

Automatic resonance suppressor (JUCE, VST3 / AU / Standalone). Full user guide: `Muzi Tame Manual.pdf`.

![Muzi Tame](screenshot.png)

## What's new in v0.7.0 (rebuilt for smooth real-time use)
Same sound. Measured on GitHub's Intel Mac (the same chip generation as a 2019 MacBook Pro), v0.6.3 still had audio blocks running past their deadline at 128-sample buffers, and the window redraw cost about half a CPU core.
- **The analysis has its own thread.** The heavy part (spectra, note detection, deciding the cuts) used to run inside the audio callback, so some blocks cost several times others and small buffers dropped out. It now runs on a worker thread next to the audio. The audio thread only runs the filters: a small, flat cost per block at any buffer size.
  - Offline bounces still run everything on one thread, sample-exact, so renders are identical to v0.6.3.
  - In real time the cuts are worked out a few milliseconds later than before (within 0.3 dB on the test material). If the computer is badly overloaded the cuts lag behind, but the audio never stops.
- **Lighter window.**
  - The analyser and the meters now take turns (60 Hz timer, each at 30 fps). macOS merges areas that change in the same frame, so before this every knob between them was redrawn 30 times a second.
  - The v0.6.3 whole-window software cache made things slower on Intel and is gone.
  - The cut curve's glow is drawn with cheaper joins.
- `tests/perf` now reports the audio thread and the analysis worker separately, and compares the window redraw strategies.

## What's new in v0.6.2 (the real cause of the lag)
- **Xcode's Debug build was unoptimised (-O0).** Opening the .jucer in Xcode and pressing Build or Run builds the *Debug* configuration, and that had optimisation switched off. Every speed test so far measured Release, so they never saw it. Measured on the same engine: -O0 costs about 3.5x the CPU of -O3 on average, and single audio blocks regularly ran past their real-time deadline (dropouts). The analyser drawing gets the same penalty, which is what made the whole Mac stutter.
  - Debug is now -O3 in every exporter (Xcode, Visual Studio, Linux), so whichever button you press you get a fast plugin. Debug still keeps its debug symbols and checks.
  - If an unoptimised build is ever loaded, the footer shows **SLOW UNOPTIMISED BUILD** in red.
  - The Mac CI build now fails if any Xcode configuration is -O0.
- **New instances start in Live mode (0 ms latency)**, so nothing arrives late while recording or monitoring. Switch to Standard or High-res for mixing, where the DAW compensates the delay and the look-ahead catches more short harshness. Saved sessions keep the mode they were saved with.

## What's new in v0.6.1 (measured on real Macs)
`.github/workflows/perf.yml` runs `tests/perf/Perf.cpp` on GitHub's Intel Mac (i7-8700B, the same chip generation as a 2019 MacBook Pro) and on Apple Silicon (M2 Pro). It times every audio block against its real-time budget, and times the display drawn with macOS's own renderer at Retina resolution. The full tables are in each run's Summary.
- **The display was the main load on the Mac.** CoreGraphics took about 49% of a core on Intel at 30 fps. The analyser is now drawn by JUCE's software renderer straight into a native bitmap.
  - Intel: Full 22%, Light 11%, Off 5% of one core.
  - Apple Silicon: Full 13%, Light 7%, Off 2.5%.
- **Audio, one instance at 44.1/48 kHz:**
  - Intel: about 16–19% of a core on average. Apple Silicon: about 8–9%.
  - Use 128 samples or more with High-res. At 64 samples its analysis lands in one block out of four.
  - 96 kHz roughly doubles the cost.

## What's new in v0.6 (latency and CPU)
- **New LIVE mode: zero latency.** Use it for recording, monitoring and live sound.
  - Standard (32 ms) and High-res (91 ms) delay the audio so the analysis can look ahead. Live doesn't delay the audio at all. This is the same approach as the tracking modes in soothe3 (0 samples at 44.1/48 kHz) and Gullfoss LIVE (under 2 ms).
  - The cut filters are minimum-phase IIR bells, so they add no delay of their own. In Live the analysis uses an asymmetric window that weights the newest audio most, so it reacts sooner without losing frequency detail.
  - Trade-off, measured on the Resonance Lab tests: steady resonances are cut almost as much as in Standard (2.4k/5.2k/9k within 0.3 dB). Short, sudden harshness gets about half the cut (JOJO harsh 5.6 kHz: -1.6 dB vs -3.6 dB), because nothing can react before a sound has happened. Use Standard or High-res for mixing.
  - The CPU cost is the same as Standard.
- **Analyser: Full / Light / Off** (the button in the footer). Light halves the display's frame rate. Off hides the spectrum and shows only the cut curve. Use it on older Macs or in big sessions. The sound does not change, and the setting is saved with the session.
- **About 10% less DSP CPU.** A fast, exact-to-1e-12 log replaces the libm log10 calls in the per-bin loops. Output is identical to v0.5.1 within -168 dBFS.

## What's new in v0.5.1 (performance)
Same sound, much lighter on the computer. On the Resonance Lab tests every score matches v0.5 within normal run-to-run variation.
- **No more CPU spikes.** v0.5 did all of the long low-end analysis on one hop out of four, so every 11 ms one audio block cost about 3x the others. At small host buffers that caused dropouts and a sluggish Mac. The work is now spread over consecutive hops. The worst hop at 48 kHz went from 34% to 15% of its time budget, and at 96 kHz High-res from 111% to 24%.
- **About 35% less CPU on average** (realtime factor 4.7x -> 7.5x on the Lab set).
  - A sliding running median replaces a sort for every point.
  - Note protection is only worked out for the low-end bins that actually use it.
  - The two channels' filter chains run side by side.
- **Lighter GUI.**
  - The analyser draws its background from a cached image.
  - The cut curve is calculated from the filter coefficients once per frame, not with trig for every pixel. That makes the update 3x faster.
  - The display is opaque, so the window behind it is no longer redrawn 30 times a second.
  - Fewer points per curve.
  - Meters and the footer only redraw when they change.
  - Nothing redraws once the audio stops.
- `tests/snapshot/UiBench.cpp` (CMake target `MuziUiBench`) measures the GUI's cost per frame.

## What's new in v0.5
- **Polyphonic Tonal Guard.** v0.4 only protected the harmonics of one note, so melodies, arpeggios and chords were cut as if they were resonances. v0.5 finds up to four notes at once (harmonic summation with estimate-and-cancel), also on a long low-end spectrum for bass notes, and tells resonances from melody by how long a peak stays at one frequency. Note partials are protected in proportion, however loud they are.
- **Punch** (transient guard). A fast/slow envelope detector, read ahead through the latency buffer, eases every cut above ~250 Hz for the moment a drum hit or pluck passes, so hits keep their snap while the ring between them is still cut.
- **Low-end resolution.** Regions below 300 Hz now read a 4x longer FFT, so narrow bass and room resonances can be resolved. Low bells may not move faster than ~2 cycles of their own frequency, which stops them reshaping bass waveforms and raising peaks.
- **CPU path.** Plugin builds use `juce::dsp::FFT` (Apple vDSP on macOS). Results are identical to the built-in FFT.
- Pitch estimates use a weighted median of the matched partials, so a partial borrowed from another note no longer skews them.

Old sessions open with Punch at 50 %.

## Open and build (Mac)
Everything is inside this folder, including JUCE 9.0.5 (in `JUCE/`), so nothing else needs downloading.

1. Double-click **`MuziTame.jucer`**. It opens in Projucer with all the code in.
2. Click the Xcode button at the top (Save and Open in IDE).
3. In Xcode choose the scheme **Muzi Tame - All**, then Product > Build (a Release build runs lightest).
4. Xcode copies the plugin to `~/Library/Audio/Plug-Ins/VST3/` and `Components/` (AU). Rescan in your DAW.

You can also open `Builds/MacOSX/Muzi Tame.xcodeproj` directly. JUCE 9.0.5, macOS 12.0 target, Apple Silicon + Intel (Xcode standard architectures), VST3 + AU + Standalone. Windows: `Builds/VisualStudio2022`.
Plugin ID is unchanged (Muzi / MzTm), so sessions that already use Muzi Tame keep loading it.

CMake alternative: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release` (downloads JUCE 9.0.5).

## Controls
| Control | What it does | Default |
|---|---|---|
| Mode | Standard: 2048-pt analysis, 32 ms latency. High-res: 4096-pt, 91 ms latency, faster attack, finer detail (mixing/mastering) | Standard |
| Depth | Scales every cut | 100 % |
| Threshold | dB above the local spectral envelope before cutting starts | 2 dB |
| Sharpness | Q of the narrow (moving) bells | 12 |
| Attack / Release | How fast cuts engage / let go | 3 ms / 60 ms |
| Punch | Eases cuts during drum hits and plucks (above ~250 Hz) | 50 % |
| Broad | Amount of the slow, wide "tonal balance" layer | 30 % |
| Tonal Guard | Protects musical notes: melodies, chords, harmonics, bass notes. Lower it to chase moving or on-and-off resonances harder | 80 % |
| Max Cut | Deepest any band may go | 12 dB |
| Mix | Dry/wet | 100 % |
| Output | Output trim | 0 dB |
| Low / High Limit | Processing range (drag handles in the display) | 20 Hz / 20 kHz |
| Delta | Hear only what is removed | off |
| Bypass | Power button; keeps latency | off |

## Measured (Resonance Lab, defaults, through the built VST3)
| Test | v0.4 | v0.5 Standard | v0.5 High-res |
|---|---|---|---|
| Latency | 1536 | 1536 samples (32 ms) | 4352 samples (91 ms) |
| Planted peaks 650 / 2400 / 5200 / 9000 Hz | −3.6 / −2.7 / −5.6 / −2.0 dB | −3.3 / −2.5 / −5.1 / −1.8 dB | −2.9 / −2.3 / −4.9 / −2.0 dB |
| Clean pink noise, worst cut | −0.29 dB | −0.03 dB | −0.01 dB |
| Fast melody (arpeggio), worst 1/3 octave | −1.41 dB | −0.18 dB | −0.71 dB |
| Sustained chord, worst 1/3 octave | −3.58 dB | −1.16 dB | −0.11 dB |
| Chord notes (mean / worst partial) | −4.5 / −11.5 dB | −0.16 / −0.9 dB | −0.42 / −1.9 dB |
| Real resonance hidden in the chord | −1.24 dB | −1.05 dB | −1.59 dB |
| Drum hits: peak change / ring cut between hits | −0.34 / −4.7 dB | −0.15 / −4.6 dB | −0.16 / −4.6 dB |
| Pitched 220 Hz note | −0.12 dB | −0.04 dB | −0.49 dB |
| Attack / release, gated resonance | 29 / 16 ms | 38 / 16 ms | 14 / 16 ms |
| Moving resonance 300 Hz → 6 kHz | −3.7 dB | −0.6 dB | −0.7 dB |

On a real 2-minute master (JOJO), full range, High-res: the melodic intro/outro lose 0.3–0.8 dB below 2 kHz (v0.4: 2–3 dB), the 5.6 kHz spike is cut 4.3 dB, bass is untouched (−0.1 dB), true peak is unchanged, and fewer drum hits lose punch (6 windows vs 17).

The built VST3 gives the same numbers as the engine, at any block size. Bypass output equals the delayed input exactly; state round-trips.

## Trade-offs to know
- **Moving or on-and-off resonances** are cut less than in v0.4, because the guard can't tell them from melody notes. Lower Tonal Guard to chase them harder (at some cost to melodies), or narrow the range around them.
- **Wide low bumps** (room modes wider than about Q 8) are left mostly to the Broad layer. Narrow low resonances are resolved better than before, but cuts there stay gentle.
- **CPU:** ~18 % of one x86 core per stereo instance with the built-in FFT (Linux test machine); the macOS build uses vDSP and should be lighter (not measured here).

## Layout
- `MuziTame.jucer` - Projucer project (+ generated `Builds/` and `JuceLibraryCode/`); `JUCE/modules` - JUCE 9.0.5, bundled
- `Source/FrankenTameEngine.h` — the DSP, header-only, no JUCE dependency (optional JUCE FFT), no allocation after `prepare()`
- `Source/PluginProcessor.*` — parameters, presets, both modes pre-prepared (switching never allocates), latency reporting
- `Source/PluginEditor.*` — analyser, cut curve, range handles, knobs, meters
- `Source/MuziLookAndFeel.h` — the palette and drawing style (re-skin from one place)
- `tests/render_engine.py` — renders the Resonance Lab signals through the engine (`python tests/render_engine.py ../resonance_lab [--high-res]`)
- `tests/scorecard.py` — one-line-per-test summary of a render folder
- `tests/host/HostTest.cpp` — headless VST3 host: loads the built plugin, sets parameters by display name, renders, checks state (`cmake -B build -DMT_BUILD_TEST_HOST=ON`; `--rate` sets the sample rate)
- `tests/snapshot/Snapshot.cpp` — renders the editor to PNG with audio running: `MuziSnapshot in.wav out.png [width] [preset]`
