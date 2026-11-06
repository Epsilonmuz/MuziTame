// C wrapper so the Lab (Python/ctypes) can drive the real-time engine
// exactly like a host: in blocks, in place, planar channels.
#include "../Source/FrankenTameEngine.h"

static float g_low = 20.0f, g_high = 20000.0f, g_tguard = 0.5f;
extern "C" {

void* ft_create (double fs, int ch, int highRes, int delay)
{
    auto* e = new muzi::FrankenTameEngine();
    e->prepare (fs, ch, highRes, delay);   // 0 Standard, 1 High-res, 2 Live
    return e;
}
void ft_destroy (void* e) { delete static_cast<muzi::FrankenTameEngine*> (e); }
int ft_latency (void* e) { return static_cast<muzi::FrankenTameEngine*> (e)->getLatencySamples(); }

void ft_set (void* e, float depth, float sens, float ratio, float att, float rel,
             float sharp, float broad, float guard, float mix, int delta)
{
    muzi::FrankenParams p;
    p.depth = depth; p.sensitivityDb = sens; p.ratio = ratio; p.attackMs = att; p.releaseMs = rel;
    p.sharpness = sharp; p.broadDepth = broad; p.tonalGuard = guard; p.mix = mix; p.delta = delta != 0;
    p.lowHz = g_low; p.highHz = g_high; p.transientGuard = g_tguard;
    static_cast<muzi::FrankenTameEngine*> (e)->setParams (p);
}
void ft_set_range (float lo, float hi) { g_low = lo; g_high = hi; }
void ft_set_tguard (float v) { g_tguard = v; }
void ft_live_tune (int nfft, int hop, int lowFactor, double fallFrac, double smoothMs)
{
    auto& t = muzi::FrankenTameEngine::liveTuning();
    t.nfft = nfft; t.hop = hop; t.lowFactor = lowFactor; t.fallFrac = fallFrac; t.smoothMs = smoothMs;
}

// x: planar float32, ch * n, processed in host-sized blocks
void ft_process (void* e, float* x, int ch, int n, int block)
{
    auto* eng = static_cast<muzi::FrankenTameEngine*> (e);
    float* ptrs[8];
    for (int s = 0; s < n; s += block)
    {
        const int len = std::min (block, n - s);
        for (int c = 0; c < ch; ++c) ptrs[c] = x + (size_t) c * n + s;
        eng->process (ptrs, len);
    }
}
}
