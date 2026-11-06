"""Render the Resonance Lab signals through the C++ engine (as a host would),
then score them with the Lab's analyzer.

  python tests/render_engine.py ../resonance_lab [--high-res] [--block 512]
"""
import argparse, ctypes, glob, os, subprocess, sys, time
import numpy as np
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))


def build():
    lib = os.path.join(HERE, "libft.so")
    src = os.path.join(HERE, "engine_capi.cpp")
    hdr = os.path.join(HERE, "..", "Source", "FrankenTameEngine.h")
    if not os.path.exists(lib) or os.path.getmtime(lib) < max(os.path.getmtime(src), os.path.getmtime(hdr)):
        subprocess.run(["g++", "-O2", "-std=c++17", "-Wall", "-Wextra", "-shared", "-fPIC", src, "-o", lib], check=True)
    so = ctypes.CDLL(lib)
    so.ft_create.restype = ctypes.c_void_p
    so.ft_create.argtypes = [ctypes.c_double, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    so.ft_latency.argtypes = [ctypes.c_void_p]
    so.ft_destroy.argtypes = [ctypes.c_void_p]
    so.ft_set.argtypes = [ctypes.c_void_p] + [ctypes.c_float] * 9 + [ctypes.c_int]
    so.ft_process.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_int, ctypes.c_int, ctypes.c_int]
    return so


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("lab")
    ap.add_argument("--high-res", action="store_true")
    ap.add_argument("--live", type=float, nargs=5, default=None, metavar=("NFFT", "HOP", "LOWFACTOR", "FALL", "SMOOTHMS"))
    ap.add_argument("--block", type=int, default=512)
    ap.add_argument("--name", default=None)
    ap.add_argument("--delay", type=int, default=-1)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--range", type=float, nargs=2, default=None)
    ap.add_argument("--tguard", type=float, default=None)
    ap.add_argument("--guard", type=float, default=0.8)
    a = ap.parse_args()
    so = build()
    if a.live:
        so.ft_live_tune.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_double, ctypes.c_double]
        so.ft_live_tune(int(a.live[0]), int(a.live[1]), int(a.live[2]), a.live[3], a.live[4])
    if a.tguard is not None:
        so.ft_set_tguard.argtypes = [ctypes.c_float]
        so.ft_set_tguard(a.tguard)
    if a.range:
        so.ft_set_range.argtypes = [ctypes.c_float, ctypes.c_float]
        so.ft_set_range(*a.range)
    name = a.name or ("engine_hr" if a.high_res else "engine")
    out = os.path.join(a.lab, "renders", name)
    os.makedirs(out, exist_ok=True)
    total_t = total_s = 0
    for path in sorted(glob.glob(os.path.join(a.lab, "signals", "*.wav"))):
        fs, x = wavfile.read(path)
        x = np.ascontiguousarray(x.T.astype(np.float32))           # planar
        ch, n = x.shape
        e = so.ft_create(fs, ch, 2 if a.live else int(a.high_res), a.delay)
        so.ft_set(e, 1.0, 2.0, 8.0, 3.0, 60.0, 12.0, 0.3, a.guard, 1.0, 0)
        t0 = time.perf_counter()
        so.ft_process(e, x.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), ch, n, a.block)
        total_t += time.perf_counter() - t0; total_s += n / fs
        lat = so.ft_latency(e)
        so.ft_destroy(e)
        wavfile.write(os.path.join(out, os.path.basename(path)), fs, x.T.copy())
    print(f"latency {lat} samples; realtime factor {total_s / total_t:.1f}x (stereo, 1 core)")
    sys.path.insert(0, a.lab)
    import analyze
    res, _ = analyze.analyze_plugin(out)
    import json
    if a.quiet:
        g = res.get("gated_res", {}); s = res.get("static_res", {}).get("cut_at_res_db", {})
        print(name, "lat", lat, "attack", g.get("attack_ms_to_90pct"), "release", g.get("release_ms_to_90pct"),
              "depth", g.get("gr_depth_db"), "static", s, "pink", res.get("pink_clean"), "harm", res.get("harmonic_220"),
              "moving", res.get("moving_res"), "prering", res.get("impulses"))
    else:
        print(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
