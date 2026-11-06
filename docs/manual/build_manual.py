"""Builds the Muzi Tame manual (HTML -> PDF) from the pictures MuziManualShots renders.

  MuziManualShots <signal.wav> docs/manual/img 0
  python3 docs/manual/build_manual.py            # writes docs/manual/manual.html
  google-chrome --headless --print-to-pdf="Muzi Tame Manual.pdf" --no-pdf-header-footer docs/manual/manual.html
"""
import json, os, html

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = "img"
parts = json.load(open(os.path.join(HERE, IMG, "parts.json")))
W, H = parts["width"], parts["height"]
VERSION = "0.8.0"

# ------------------------------------------------------------------ numbered overview
callouts = [  # number, part, label, (dx, dy) offset from the part's top-left, in window pixels
    (1, "prev", "Preset bar", (-6, -6)),
    (2, "save", "Save preset", (-6, -6)),
    (3, "live", "Mode", (-6, -6)),
    (4, "delta", "Delta", (-6, -6)),
    (5, "bypass", "Bypass", (-6, -6)),
    (6, "display", "Display", (440, 120)),
    (7, "display", "Range handles", (6, 36)),
    (8, "attack", "Attack", (4, 56)),
    (9, "release", "Release", (4, 56)),
    (10, "punch", "Punch", (4, 56)),
    (11, "threshold", "Threshold", (4, 56)),
    (12, "depth", "Depth", (14, 4)),
    (13, "sharpness", "Sharpness", (4, 56)),
    (14, "broad", "Broad", (4, 56)),
    (15, "guard", "Tonal Guard", (4, 56)),
    (16, "maxcut", "Max Cut", (4, 56)),
    (17, "mix", "Mix", (2, 2)),
    (18, "trim", "Output", (2, 2)),
    (19, "meters", "IN / OUT meters", (4, -14)),
    (20, "analyser", "Analyser button", (-14, -4)),
]

def badge(n, part, dx, dy):
    x, y, w, h = parts[part]
    return f'<span class="badge" style="left:{100*(x+dx)/W:.2f}%;top:{100*(y+dy)/H:.2f}%">{n}</span>'

overview = "".join(badge(n, p, *d) for n, p, _, d in callouts)
legend = "".join(f"<li><b>{n}</b> {html.escape(l)}</li>" for n, _, l, _ in callouts)

# ------------------------------------------------------------------ control cards
def card(img, title, num, rng, default, body, tips=None, wide=False):
    tip = f'<p class="tip"><b>Try:</b> {tips}</p>' if tips else ""
    meta = ""
    if rng:
        meta = f'<div class="meta"><span>Range <b>{rng}</b></span><span>Default <b>{default}</b></span></div>'
    return f"""
<section class="card{' wide' if wide else ''}">
  <div class="pic"><img src="{IMG}/{img}.png" alt="{html.escape(title)}"></div>
  <div class="txt">
    <h3><span class="n">{num}</span>{html.escape(title)}</h3>
    {meta}
    {body}
    {tip}
  </div>
</section>"""

def pair(a, b, cap_a, cap_b):
    return f"""<div class="pair"><figure><img src="{IMG}/{a}.png"><figcaption>{cap_a}</figcaption></figure>
<figure><img src="{IMG}/{b}.png"><figcaption>{cap_b}</figcaption></figure></div>"""

header_cards = "".join([
    card("presetbar", "Preset bar", 1, None, None,
         "<p>The menu in the middle shows the loaded preset. <b>&lt;</b> and <b>&gt;</b> step through all presets: the factory ones first, then your own. "
         "Click the name to open the menu, which has two sections, <b>Factory</b> and <b>Your presets</b>, plus <i>Save as new preset…</i>, "
         "<i>Delete</i> (when one of your presets is loaded) and <i>Show presets folder</i>.</p>"
         + pair("presetbar", "presetbar_modified", "Just loaded", "A knob has moved since: the name gets a *")
         + "<p>The <b>*</b> after the name means you have changed something since the preset was loaded. Save to keep it, or reload the preset to go back.</p>"
         "<p>Presets set the sound controls (knobs 8–18 and the range handles). They never change Mode, Delta or Bypass, so loading one while recording keeps you in Live.</p>",
         wide=True),
    card("save", "SAVE", 2, None, None,
         "<p>Saves everything the knobs and range handles are set to as <b>your own preset</b>. Type a name and press Save (or Return). "
         "If the name is already used, Muzi Tame asks before replacing it. When one of your presets is loaded, its name is already filled in, so "
         "tweak, SAVE, Return updates it.</p>"
         "<p>Your presets appear in every Muzi Tame, in every session and every DAW, under <b>Your presets</b>. See <i>Your own presets</i> for where the files live.</p>"),
    card("modes", "Mode: LIVE / STANDARD / HI-RES", 3, "3 modes", "LIVE",
         "<p><b>LIVE</b> – 0 ms latency. For recording, monitoring and live sound. Steady resonances are cut almost as much as in Standard; short, sudden harshness gets about half the cut, because nothing can react before a sound has happened.</p>"
         "<p><b>STANDARD</b> – 32 ms latency. The everyday mixing mode: the audio is delayed so the analysis can look ahead and the cut is ready when the resonance arrives.</p>"
         "<p><b>HI-RES</b> – 91 ms latency, faster attack and finer low-end detail. For mixing and mastering, where the DAW compensates the delay.</p>",
         "Record in LIVE, switch to STANDARD or HI-RES when you mix. Some hosts only pick up the new latency after you stop and restart playback."),
    card("delta", "DELTA", 4, "on / off", "off",
         "<p>Plays <b>only what Muzi Tame is removing</b>. Use it to check that you hear thin ringing, whistle or boom, not the main tone of the instrument.</p>"
         + pair("delta", "delta_on", "Off: you hear the processed sound", "On (lit): you hear only what is removed"),
         "if Delta sounds like the instrument itself (its body, its note), lower Depth or raise Threshold."),
    card("bypass", "Bypass (power button)", 5, "on / off", "processing",
         "<p>Switches the processing off. The latency stays the same while bypassed, so A/B comparisons stay in time.</p>"
         + pair("bypass", "bypass_on", "Teal: processing", "Red: bypassed"),
         "set Output so the level is the same with and without bypass before you judge it. Louder always sounds better."),
])

display_cards = "".join([
    card("display", "Display", 6, None, None,
         "<p><b>Grey fill</b>: the spectrum coming in. <b>Thin light line</b>: the spectrum going out. "
         "<b>Teal curve</b>: the total cut right now, read against the dB scale on the right (0 to −18 dB). "
         "<b>Teal dots</b>: each moving cut that is active. <b>Gold line</b>: the slow, wide Broad layer.</p>"
         "<p>Hover over the display to read the frequency and the cut at that point.</p>", wide=True),
    card("range_low", "Range handles: LOW and HIGH", 7, "20 Hz – 20 kHz", "20 Hz / 20 kHz",
         "<p>The two gold lines set where Muzi Tame is allowed to work. Drag a line (or its tag) left or right; double-click it to reset. "
         "Nothing outside the range is cut, and cuts fade out over a third of an octave past each line, so there is no hard edge.</p>"
         + pair("range_low", "range_high", "LOW handle (left edge)", "HIGH handle (right edge)"),
         "narrow the range to just the problem: 1.5–12 kHz for vocal harshness, 30–300 Hz for 808 boom. A narrow range is the easiest way to stop it touching what you like.",
         wide=True),
])

knob_cards = "".join([
    card("attack", "Attack", 8, "0.5 – 50 ms", "3 ms",
         "<p>How quickly a cut reaches full depth once a resonance appears. Short = catches sudden harshness; longer = lets the start of each sound through untouched.</p>",
         "2–5 ms for vocals, 1 ms for drum ring, 10–15 ms on a mix bus."),
    card("release", "Release", 9, "10 – 500 ms", "60 ms",
         "<p>How quickly a cut lets go after the resonance fades. Short follows the music closely; long is smoother but can hold a cut after the problem has gone.</p>",
         "40–80 ms on most sources, 100–200 ms on a mix bus or long 808s."),
    card("punch", "Punch", 10, "0 – 100 %", "50 %",
         "<p>Keeps drum hits and plucks snappy. For the moment a transient passes, every cut above about 250 Hz is eased off, so the hit keeps its snap while the ring between hits is still cut.</p>",
         "turn it up on drums and percussive guitar; turn it down if harsh consonants or pick clicks slip through."),
    card("threshold", "Threshold", 11, "−6 – +12 dB", "2 dB",
         "<p>How far a peak has to rise above its surroundings before it is cut. <b>Lower catches more</b>; higher only touches the most obvious peaks.</p>",
         "work Depth first, then lower Threshold if smaller resonances are still poking out."),
    card("depth", "Depth", 12, "0 – 200 %", "100 %",
         "<p>The main control: how hard every cut is. 100 % is the calibrated amount; past 100 % is for heavy taming. "
         "At 0 % nothing is cut. Depth scales the cut, Max Cut caps it.</p>",
         "start at 100 % and turn down until the problem is just gone. Over-taming makes sources dull and flat."),
    card("sharpness", "Sharpness", 13, "4 – 40", "12",
         "<p>The width of each moving cut. Higher = narrower, more surgical; lower = wider and gentler, closer to a dynamic EQ.</p>",
         "16–20 for snare and tom ring (a single steady tone); 8–10 for broad boxiness or mix-bus work."),
    card("broad", "Broad", 14, "0 – 100 %", "30 %",
         "<p>The amount of the slow, wide “tonal balance” layer (the gold curve) on top of the narrow cuts. It gently evens out regions that are too strong overall rather than single peaks.</p>",
         "raise it to tame a generally bright or boomy source; set 0 % for narrow cuts only."),
    card("guard", "Tonal Guard", 15, "0 – 100 %", "80 %",
         "<p>Protects musical notes – melodies, chords, harmonics and bass notes – so only real resonances are cut. Muzi Tame tracks up to four notes at once and leaves their harmonics alone.</p>"
         "<p>Lower it to chase resonances that move or come and go, at the risk of touching notes.</p>",
         "on 808s that boom on some notes and not others, 40–55 % lets it even the notes out (that is what the 808 preset does)."),
    card("maxcut", "Max Cut", 16, "3 – 24 dB", "12 dB",
         "<p>The deepest any single cut may go, however strong the resonance. A safety limit.</p>",
         "4–6 dB for mastering, 9–10 dB on bass and 808s so the weight stays, 14–18 dB for heavy ring."),
    card("mix", "Mix", 17, "0 – 100 %", "100 %",
         "<p>Blend between the original and the processed sound. Below 100 % some of the original sound is mixed back in, so every cut is effectively shallower.</p>",
         "70–80 % for a lighter touch that keeps some of the original edge."),
    card("trim", "Output", 18, "−12 – +12 dB", "0 dB",
         "<p>Output level. Cutting resonances lowers the level a little; use Output to match bypass so you compare fairly.</p>"),
    card("meters", "IN / OUT meters", 19, None, None,
         "<p>Peak level going in and coming out, with a short hold line. If OUT is clearly lower than IN, raise Output to match.</p>"),
    card("analyser", "Analyser button", 20, "Full / Light / Off", "Full",
         "<p>How much the display draws: <b>Full</b>, <b>Light</b> (half the frame rate) or <b>Off</b> (only the cut curve). "
         "Lighter settings help older or busy computers. The sound does not change, and the setting is saved with the session.</p>",
         "Light or Off on a 2019 Intel MacBook Pro with many instances open."),
])

presets = [
    ("Default", "General starting point."),
    ("Vocal - De-Harsh", "1.5–12 kHz, harder and narrower: harshness, whistle and sibilant ring in vocals."),
    ("Vocal - Body & Boxiness", "150 Hz–1.5 kHz, wider: boxy, honky or muddy vocal body."),
    ("Acoustic Guitar", "200 Hz–8 kHz, high Tonal Guard so chords stay intact."),
    ("Drum Bus - Ring Control", "150 Hz–6 kHz, fast and narrow, high Punch: snare and tom ring without losing hits."),
    ("Mix Bus - Polish", "Full range, gentle and slow, more Broad."),
    ("Mastering - Gentle", "Full range, very light, Max Cut 4 dB."),
    ("Aggressive", "Everything turned up: heavy taming or sound design."),
    ("808 & Bass - Even Out", "<b>New in 0.8.</b> 20 Hz–5 kHz, Tonal Guard 45 %, Release 100 ms, Max Cut 10 dB, Output +1 dB. "
     "For 808s and bass that boom on some notes and not others. On a test 808 line through a room mode it cut the note-to-note level spread from 3.0 to 2.4 dB and a 165 Hz box resonance by 5 dB."),
]
preset_rows = "".join(f"<tr><td>{n}</td><td>{d}</td></tr>" for n, d in presets)

page = f"""<!doctype html><html><head><meta charset="utf-8"><title>Muzi Tame Manual</title>
<style>
@page {{ size: Letter; margin: 14mm 14mm 16mm; }}
:root {{ --ink:#1b1e24; --soft:#5b6270; --line:#dfe3ea; --teal:#2bb3a3; --gold:#d9a23a; --bg:#f6f8fa; }}
* {{ box-sizing: border-box; }}
body {{ font-family: Inter, "Helvetica Neue", Arial, sans-serif; color: var(--ink); font-size: 10.4pt; line-height: 1.45; margin: 0; }}
h1 {{ font-size: 28pt; letter-spacing: .06em; margin: 0; }}
h2 {{ font-size: 16pt; margin: 22px 0 8px; padding-bottom: 4px; border-bottom: 2px solid var(--teal); break-after: avoid; }}
h3 {{ font-size: 12pt; margin: 0 0 4px; display:flex; align-items:center; gap:8px; }}
p {{ margin: 0 0 7px; }}
.sub {{ color: var(--soft); letter-spacing: .2em; font-size: 9pt; text-transform: uppercase; }}
.cover {{ display:flex; justify-content:space-between; align-items:flex-end; margin-bottom: 10px; }}
.ver {{ color: var(--soft); font-size: 9.5pt; text-align:right; }}
.hero {{ position: relative; border-radius: 10px; overflow: hidden; }}
.hero img {{ width: 100%; display:block; }}
.badge {{ position:absolute; transform: translate(-50%,-50%); background: var(--gold); color:#111; font-weight:700; font-size:8.5pt;
         width:19px; height:19px; border-radius:50%; display:flex; align-items:center; justify-content:center; box-shadow:0 0 0 2px #111; }}
ol.legend {{ list-style:none; padding:0; columns: 4; column-gap: 14px; margin: 8px 0 0; font-size: 9.3pt; }}
ol.legend b {{ display:inline-block; width:18px; color: #9a6c12; }}
.n {{ background: var(--gold); color:#111; font-size:9pt; min-width:20px; height:20px; border-radius:50%; display:inline-flex; align-items:center; justify-content:center; }}
.card {{ display:flex; gap:14px; align-items:flex-start; padding: 10px 0; border-bottom: 1px solid var(--line); break-inside: avoid; }}
.card .pic {{ flex: 0 0 auto; }}
.card .pic img {{ border-radius: 8px; display:block; max-width: 150px; max-height: 190px; }}
.card.wide {{ flex-direction: column; }}
.card.wide .pic img {{ max-width: 100%; max-height: 260px; }}
.card .txt {{ flex: 1; }}
.meta {{ display:flex; gap:16px; color: var(--soft); font-size: 9pt; margin-bottom: 5px; }}
.meta b {{ color: var(--ink); }}
.tip {{ background: var(--bg); border-left: 3px solid var(--teal); padding: 5px 9px; border-radius: 4px; font-size: 9.5pt; }}
.pair {{ display:flex; gap: 12px; margin: 6px 0 8px; flex-wrap: wrap; }}
.pair figure {{ margin:0; }}
.pair img {{ border-radius: 6px; display:block; max-height: 150px; max-width: 330px; }}
figcaption {{ font-size: 8.5pt; color: var(--soft); margin-top: 3px; }}
table {{ width:100%; border-collapse: collapse; font-size: 9.6pt; margin: 6px 0; }}
td, th {{ border-bottom: 1px solid var(--line); padding: 5px 6px; text-align:left; vertical-align: top; }}
th {{ background: var(--bg); }}
td:first-child {{ white-space: nowrap; font-weight: 600; }}
ol.steps li {{ margin-bottom: 4px; }}
code {{ background: var(--bg); padding: 1px 4px; border-radius: 3px; font-size: 9pt; }}
.break {{ break-before: page; }}
.panels {{ display:flex; gap:8px; align-items:flex-start; margin: 6px 0 4px; }}
.panels img {{ height: 120px; border-radius: 6px; }}
</style></head><body>

<div class="cover">
  <div><h1>MUZI TAME</h1><div class="sub">Resonance suppressor · User manual</div></div>
  <div class="ver">Version {VERSION}<br>VST3 · AU · Standalone</div>
</div>

<p>Muzi Tame finds harsh, ringing, boomy or boxy frequencies as they happen and turns them down only while they stick out. It is automatic: you tell it how hard, where, and how fast, and it does the rest. This manual shows a picture of every control and explains what it does.</p>

<div class="hero"><img src="{IMG}/full.png" alt="Muzi Tame">{overview}</div>
<ol class="legend">{legend}</ol>

<h2>Quick start</h2>
<ol class="steps">
<li>Insert Muzi Tame on a track or bus, after corrective EQ and before compression or saturation.</li>
<li>Pick a preset close to your job (<b>1</b>). For 808s, start with <i>808 &amp; Bass - Even Out</i>.</li>
<li>Drag the LOW and HIGH handles (<b>7</b>) around the problem area.</li>
<li>Turn on DELTA (<b>4</b>) and adjust Depth (<b>12</b>) until you hear only the ringing or boom, not the instrument itself. Turn DELTA off.</li>
<li>Match the level with Output (<b>18</b>) and compare with Bypass (<b>5</b>).</li>
<li>Click SAVE (<b>2</b>) to keep your settings as a preset.</li>
</ol>
<p>Recording or monitoring? Stay in <b>LIVE</b> (no delay). Mixing? Switch to <b>STANDARD</b> or <b>HI-RES</b> for faster, more precise cuts.</p>

<h2>Top bar</h2>
{header_cards}

<h2>Display and range</h2>
{display_cards}

<h2>The knobs</h2>
<p>The knobs sit in four panels. Drag up and down to turn one; <b>double-click any knob to reset it</b>. Hover over a control to see a short tip. Drag the bottom-right corner of the window to resize it.</p>
<div class="panels">
<img src="{IMG}/panel_dynamics.png"><img src="{IMG}/panel_tame.png"><img src="{IMG}/panel_character.png"><img src="{IMG}/panel_output.png">
</div>
<p><b>DYNAMICS</b> – how fast cuts come and go. <b>TAME</b> – how much is cut. <b>CHARACTER</b> – what kind of cut. <b>OUTPUT</b> – blend, level and meters.</p>
{knob_cards}

<h2>Factory presets</h2>
<p>Starting points tuned on test signals; adjust by ear on your material. They also appear in your DAW's program list.</p>
<table><tr><th>Preset</th><th>What it is for</th></tr>{preset_rows}</table>

<h2>Your own presets</h2>
<p>Click <b>SAVE</b>, type a name, press Return. That's it: the preset is now under <b>Your presets</b> in the menu of every Muzi Tame.</p>
<ul>
<li><b>Update one:</b> load it, change the knobs (the name gets a *), click SAVE, press Return, confirm Replace.</li>
<li><b>Delete one:</b> load it, open the menu, choose <i>Delete "name"…</i>. It goes to the Trash.</li>
<li><b>Bring in a preset you made in your DAW</b> (for example one saved as a DAW plugin preset): load it the way you saved it, then click SAVE. From then on it lives in Muzi Tame and works in any DAW.</li>
<li><b>Where they are:</b> on a Mac, <code>~/Library/Audio/Presets/Muzi/Muzi Tame/</code>; on Windows, <code>%APPDATA%\\Muzi\\Muzi Tame\\Presets\\</code>. <i>Show presets folder</i> in the menu opens it. Each preset is one small <code>.mtpreset</code> file you can back up, copy to another computer or send to someone.</li>
<li>A preset stores the sound controls and the range. It does not store Mode, Delta or Bypass.</li>
<li>Your DAW session remembers which preset was loaded, including your own.</li>
</ul>

<h2>Modes and latency</h2>
<table>
<tr><th>Mode</th><th>Latency</th><th>Use it for</th></tr>
<tr><td>LIVE</td><td>0 ms</td><td>Recording, monitoring, live sound. New instances start here.</td></tr>
<tr><td>STANDARD</td><td>32 ms</td><td>Everyday mixing.</td></tr>
<tr><td>HI-RES</td><td>91 ms</td><td>Mixing and mastering: faster attack, finer low-end detail.</td></tr>
</table>
<p>Muzi Tame reports its latency to the host, so most DAWs keep everything in time automatically. Saved sessions keep the mode they were saved with.</p>

<h2>Workflow tips</h2>
<p><b>808s and bass.</b> Use <i>808 &amp; Bass - Even Out</i>, or set the range to about 25 Hz–5 kHz, Tonal Guard 40–55 %, Release around 100 ms and Max Cut 9–10 dB. Lower Tonal Guard evens out notes that boom more than others; higher keeps every note's weight. Raise Output a dB to make up for the level it takes off.</p>
<p><b>Vocals.</b> Range 1.5–12 kHz for harshness, 150 Hz–1.5 kHz for boxiness. Fast Attack (2–3 ms), Punch 30 %.</p>
<p><b>Drums.</b> Snare and tom ring are steady tones: higher Sharpness and fast Attack work well, with Punch high so hits stay sharp. On overheads keep the range above about 3 kHz.</p>
<p><b>Mix bus and mastering.</b> Low Depth (30–60 %), slow Attack and Release, Max Cut 4–6 dB, HI-RES mode.</p>
<p><b>Always:</b> place it before compression, check with DELTA, and stop as soon as the problem is gone.</p>

<h2 class="break">Installing and building (Mac)</h2>
<ol class="steps">
<li>Double-click <b>MuziTame.jucer</b>. It opens in Projucer with all the code in; JUCE 9.0.5 is inside the folder, so nothing else needs downloading.</li>
<li>Click the Xcode button at the top (Save and Open in IDE).</li>
<li>In Xcode choose the scheme <b>Muzi Tame - All</b>, then Product › Build.</li>
<li>Xcode copies the plugin to <code>~/Library/Audio/Plug-Ins/VST3/</code> and <code>Components/</code> (AU). Rescan plugins in your DAW.</li>
</ol>
<p>Ready-built Mac versions are also made automatically on GitHub (Actions › latest run › Artifacts), with an INSTALL.txt.</p>

<h2>Troubleshooting</h2>
<p><b>I can hardly hear a difference.</b> That is often right: it only acts on resonances. Turn on DELTA to hear what it removes, raise Depth or lower Threshold, or narrow the range to the problem.</p>
<p><b>The sound got dull or thin.</b> Lower Depth, raise Threshold, or raise Tonal Guard. Check DELTA: if you hear the instrument's main tone, it is doing too much.</p>
<p><b>The 808 lost weight.</b> Lower Max Cut to 6–9 dB, raise Tonal Guard, or move the LOW handle up to about 40 Hz so the sub is left alone.</p>
<p><b>Drum hits lost their snap.</b> Raise Punch, or lengthen Attack.</p>
<p><b>The audio arrives late while recording.</b> Switch to LIVE.</p>
<p><b>The computer is sluggish.</b> Set the Analyser button to Light or Off. Make sure it is a release-optimised build: a red <i>SLOW UNOPTIMISED BUILD</i> in the footer means it is not.</p>
<p><b>My preset isn't in the menu.</b> The menu reads the presets folder each time it opens; check the file ends in <code>.mtpreset</code> and is in the folder <i>Show presets folder</i> opens.</p>

<h2>Specifications</h2>
<table>
<tr><td>Formats</td><td>VST3, AU, Standalone (macOS 12+, Apple Silicon and Intel); VST3 on Windows</td></tr>
<tr><td>Channels</td><td>Mono or stereo</td></tr>
<tr><td>Filters</td><td>32 moving narrow bells + 20 fixed broad bells, minimum phase (no pre-ringing)</td></tr>
<tr><td>Analysis</td><td>Runs on its own thread next to the audio. Standard: 2048-point FFT; High-res: 4096-point; a 4× longer FFT below 300 Hz</td></tr>
<tr><td>Detection</td><td>Running-median spectral envelope, multi-note pitch tracking (up to 4 notes), transient detector</td></tr>
<tr><td>Latency</td><td>LIVE 0 ms, STANDARD 32 ms, HI-RES 91 ms</td></tr>
<tr><td>Window</td><td>Resizable, 720×450 to 1440×900</td></tr>
<tr><td>Presets</td><td>9 factory presets plus your own (.mtpreset files)</td></tr>
</table>
</body></html>"""

open(os.path.join(HERE, "manual.html"), "w").write(page)
print("wrote manual.html")
