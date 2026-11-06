"""One-line-per-test scorecard for an engine/plugin render folder.
  python tests/scorecard.py ../resonance_lab/renders/engine [more folders...]"""
import sys, os, json, warnings
warnings.filterwarnings("ignore")
LAB = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "resonance_lab"))
sys.path.insert(0, LAB)
import analyze

def row(r):
    o = []
    s = r.get("static_res", {}).get("cut_at_res_db", {})
    o.append("static[650/2.4k/5.2k/9k]=" + "/".join(str(s.get(k)) for k in ("650", "2400", "5200", "9000")))
    o.append("pink_worst=%s" % r.get("pink_clean", {}).get("max_cut_db"))
    o.append("harm=%s" % r.get("harmonic_220", {}).get("mean_cut_db"))
    g = r.get("gated_res", {}); o.append("att/rel=%s/%s" % (g.get("attack_ms_to_90pct"), g.get("release_ms_to_90pct")))
    o.append("moving=%s" % r.get("moving_res", {}).get("mean_cut_on_moving_peak_db"))
    m = r.get("melody", {}); o.append("MELODY worst=%s@%s" % (m.get("worst_third_octave_db"), m.get("worst_at_hz")))
    c = r.get("chord", {}); o.append("CHORD worst=%s@%s" % (c.get("worst_third_octave_db"), c.get("worst_at_hz")))
    cr = r.get("chord_res", {}); o.append("CHORD+RES res=%s partials=%s/%s" % (cr.get("cut_at_res_db"), cr.get("partials_mean_db"), cr.get("partials_worst_db")))
    d = r.get("drums_res", {}); o.append("DRUMS hitpk=%s/%s ring=%s" % (d.get("hit_peak_change_db"), d.get("hit_peak_worst_db"), d.get("ring_cut_between_hits_db")))
    lo = r.get("low_res", {}).get("cut_at_res_db", {}); o.append("LOW[70/120/180]=" + "/".join(str(lo.get(k)) for k in ("70", "120", "180")))
    for k in ("jojo_intro", "jojo_outro", "jojo_harsh"):
        j = r.get(k, {})
        if j: o.append("%s 200-2k=%s 4-8k=%s 5.6k=%s" % (k.upper(), j["bands"]["200-2k"], j["bands"]["4-8k"], j["cut_5583"]))
    return o

for folder in sys.argv[1:]:
    r, _ = analyze.analyze_plugin(folder)
    print("==", os.path.basename(folder.rstrip("/")), "latency", r.get("latency_samples"))
    for line in row(r): print("   ", line)
