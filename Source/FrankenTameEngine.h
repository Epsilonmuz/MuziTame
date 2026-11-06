/*  FrankenTameEngine — Muzi Tame v0.5 DSP core
    ------------------------------------------------------------------------
    Real-time resonance suppressor. No JUCE dependency (optionally uses
    juce::dsp::FFT when MUZI_USE_JUCE_FFT=1), no allocation after prepare().

      detector (every `hop` samples, on the mid signal)
        main spectrum: Hann FFT -> 25 ms power smoothing -> running-median
        envelope on a log-frequency grid -> robust (MAD) noise-scatter margin
        low spectrum (v0.5): a 4x longer FFT, refreshed every 4 hops, used by
        the regions below 300 Hz so narrow bass/room resonances are resolved
      tonal guard (v0.5: polyphonic)
        1) multi-pitch: up to 4 notes found by harmonic summation with
           estimate-and-cancel; their partials are protected
        2) stationarity: resonances stay at one frequency for seconds, melody
           partials hop around; peaks with little persistence are protected
        3) single-pitch upper envelope (cepstrum) for dense harmonic tones
        Protection lowers a peak's effective excess by up to 12 dB x guard.
      narrow layer (Soothe-style)
        one moving bell per ERB region, on that region's strongest unprotected
        peak, cutting only the excess above threshold
      broad layer (Gullfoss-style)
        band level vs 2-octave shape -> fixed ERB-wide bells, ridge least-squares
        log-magnitude fit (matrix built in prepare); also respects the guard
      transient guard (v0.5)
        fast/slow envelope ratio on the input, read ahead through the latency
        buffer; during hits every cut is eased so drums keep their snap
      audio path
        look-ahead delay -> cascade of TPT/ZDF SVF bells (minimum phase),
        gain and centre ramped across each hop, coefficients every 16 samples

    Latency = getLatencySamples() (Standard 1536, High-res 4352 samples at
    48 kHz). Report it to the host with setLatencySamples().
*/
#pragma once
#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wfloat-equal"
 #pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#ifndef _USE_MATH_DEFINES
 #define _USE_MATH_DEFINES   // M_PI on MSVC
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>
#if defined(MUZI_USE_JUCE_FFT) && MUZI_USE_JUCE_FFT
 #include <juce_dsp/juce_dsp.h>
#endif

namespace muzi
{

struct FrankenParams
{
    float depth         = 1.0f;   // 0..2   scales every cut
    float sensitivityDb = 2.0f;   // dB above envelope before cutting
    float ratio         = 8.0f;   // excess -> cut ratio
    float kneeDb        = 4.0f;
    float attackMs      = 3.0f;
    float releaseMs     = 60.0f;
    float maxCutDb      = 12.0f;
    float sharpness     = 12.0f;  // Q of the narrow bells
    float glideMs       = 8.0f;   // how fast a narrow bell follows a moving peak
    float broadDepth    = 0.3f;   // 0 = narrow layer only
    float tonalGuard    = 0.8f;   // 0..1 protection of musical notes (harmonics, melodies, chords)
    float transientGuard = 0.5f;  // 0..1 easing of cuts during drum hits and plucks
    float scatterK      = 1.5f;   // noise-scatter margin (higher = fewer false cuts)
    float mix           = 1.0f;
    bool  delta         = false;  // output only what is removed
    float lowHz         = 20.0f;  // processing range: cuts fade out over 1/3 octave
    float highHz        = 20000.0f; //   outside [lowHz, highHz]
};

// ---------------------------------------------------------------- real FFT
class RealFFT
{
public:
    int n = 0;
#if defined(MUZI_USE_JUCE_FFT) && MUZI_USE_JUCE_FFT
    void setup (int N)
    {
        n = N; int order = 0; while ((1 << order) < N) ++order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        buf.assign ((size_t) (2 * N), 0.0f);
    }
    void forward (const double* x, double* re, double* im)
    {
        for (int i = 0; i < n; ++i) buf[(size_t) i] = (float) x[i];
        std::fill (buf.begin() + n, buf.end(), 0.0f);
        fft->performRealOnlyForwardTransform (buf.data(), true);
        for (int k = 0; k <= n / 2; ++k) { re[k] = buf[(size_t) (2 * k)]; im[k] = buf[(size_t) (2 * k + 1)]; }
    }
private:
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> buf;
#else
    // N real points via one N/2 complex radix-2 FFT
    void setup (int N)
    {
        n = N;
        const int m = n / 2;
        int bits = 0; while ((1 << bits) < m) ++bits;
        rev.resize (m);
        for (int i = 0; i < m; ++i)
        {
            int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[i] = r;
        }
        tw.resize (m / 2);
        for (int i = 0; i < m / 2; ++i) tw[i] = std::polar (1.0, -2.0 * M_PI * i / m);
        twr.resize (m / 2); twi.resize (m / 2);
        for (int i = 0; i < m / 2; ++i) { twr[i] = tw[i].real(); twi[i] = tw[i].imag(); }
        zre.resize (m); zim.resize (m);
        twN.resize (m + 1);
        for (int k = 0; k <= m; ++k) twN[k] = std::polar (1.0, -2.0 * M_PI * k / n);
        zb.resize (m);
    }
    void forward (const double* x, double* outRe, double* outIm)
    {
        const int m = n / 2;
        double* zr = zre.data(); double* zi = zim.data();
        for (int i = 0; i < m; ++i) { zr[rev[i]] = x[2 * i]; zi[rev[i]] = x[2 * i + 1]; }
        for (int i = 0; i < m; i += 2)                       // first stage: twiddle is 1
        {
            const double ar = zr[i + 1], ai = zi[i + 1];
            zr[i + 1] = zr[i] - ar; zi[i + 1] = zi[i] - ai; zr[i] += ar; zi[i] += ai;
        }
        for (int len = 4; len <= m; len <<= 1)
        {
            const int step = m / len, half = len / 2;
            for (int s0 = 0; s0 < m; s0 += len)
                for (int k = 0; k < half; ++k)
                {
                    const double wr = twr[k * step], wi = twi[k * step];
                    const int a = s0 + k, b = a + half;
                    const double tr = zr[b] * wr - zi[b] * wi, ti = zr[b] * wi + zi[b] * wr;
                    zr[b] = zr[a] - tr; zi[b] = zi[a] - ti;
                    zr[a] += tr; zi[a] += ti;
                }
        }
        for (int k = 0; k <= m; ++k)
        {
            const int ia = k == m ? 0 : k, ib = k == 0 ? 0 : m - k;
            const double ar = zr[ia], ai = zi[ia], br = zr[ib], bi = -zi[ib];
            const double xer = 0.5 * (ar + br), xei = 0.5 * (ai + bi);
            const double xor_ = 0.5 * (ai - bi), xoi = -0.5 * (ar - br);
            outRe[k] = xer + twN[k].real() * xor_ - twN[k].imag() * xoi;
            outIm[k] = xei + twN[k].real() * xoi + twN[k].imag() * xor_;
        }
    }
private:
    std::vector<int> rev;
    std::vector<std::complex<double>> tw, twN, zb;
    std::vector<double> twr, twi, zre, zim;
#endif
};

class FrankenTameEngine
{
public:
    static constexpr int kNarrow = 32, kBroad = 20, kBands = 48, kFilters = kNarrow + kBroad;
    static constexpr int kMaxCh = 8, kCtrl = 16, kMaxPitches = 4, kMaxPeaks = 160;

    void prepare (double sampleRate, int numChannels, bool highRes = false, int delayOverride = -1)
    {
        fs = sampleRate;
        nch = std::min (numChannels, kMaxCh);
        // scale FFT/hop with sample rate so frequency and time resolution
        // stay the same at 44.1/48/88.2/96 kHz
        const int scale = sampleRate > 60000 ? 2 : 1;
        const int nfft = (highRes ? 4096 : 2048) * scale;
        hop = (highRes ? 256 : 128) * scale;
        lowEvery = 4;
        delay = delayOverride > 0 ? delayOverride : defaultDelay (highRes) * scale;

        // shared log grid for the running median
        const int nlg = (int) (std::log2 (fs / 2.0 / (fLo / 2.0)) * 48.0);
        lg.resize (nlg);
        for (int i = 0; i < nlg; ++i) lg[i] = fLo / 2.0 * std::pow (2.0, i / 48.0);
        medW = std::max (3, ((int) (envOct * 48.0)) | 1);
        pg.assign (nlg, 0.0); medG.assign (nlg, 0.0); medWin.assign (std::max (medW, (int) (lowEnvOct * 48.0) | 1), 0.0);

        setupSpec (A, nfft);
        setupSpec (L, nfft * lowFactor);
        for (auto* v : { &Lp.Pdb, &Lp.dev, &Lp.prot, &Lp.harm }) v->assign (L.nb, 0.0);
        A.medW = medW;
        L.medW = std::max (3, ((int) (lowEnvOct * 48.0)) | 1);   // wider: low bumps are wide in Hz
        ring.assign (L.n, 0.0);
        scratch.assign (std::max (A.nb, L.nb) + 8, 0.0);

        // narrow regions on the ERB scale; regions below lowSplit read the long spectrum
        auto edges = erbCenters (kNarrow + 1, fLo, fHi);
        for (int i = 0; i < kNarrow; ++i)
        {
            regC[i] = std::sqrt (edges[i] * edges[i + 1]);
            useL[i] = edges[i + 1] <= lowSplit;
            regS[i] = lowerBound (A, edges[i]);  regE[i] = lowerBound (A, edges[i + 1]);
            regSL[i] = lowerBound (L, edges[i]); regEL[i] = lowerBound (L, edges[i + 1]);
        }
        for (int i = 0; i < kNarrow; ++i)
        {
            const int a = std::max (0, i - 2), b = std::min (kNarrow - 1, i + 2);
            poolS[i] = regS[a];  poolE[i] = regE[b];
            poolSL[i] = regSL[a]; poolEL[i] = regEL[b];
        }
        lowProtEnd = 0;
        for (int i = 0; i < kNarrow; ++i) if (useL[i]) lowProtEnd = std::max ({ lowProtEnd, poolEL[i] + 1, regEL[i] + 1 });

        // broad layer: fixed bells + ridge least squares  M = (BtB + lI)^-1 Bt
        auto bc = erbCenters (kBands, fLo, fHi);
        auto fcb = erbCenters (kBroad, fLo * 1.2, fHi * 0.9);
        for (int j = 0; j < kBroad; ++j) { fcB[j] = fcb[j]; qB[j] = fcb[j] / (erbBw (fcb[j]) * 2.5); }
        std::vector<double> B (kBands * kBroad);
        for (int k = 0; k < kBands; ++k)
            for (int j = 0; j < kBroad; ++j)
                B[k * kBroad + j] = bellResponseDb (bc[k], fcB[j], qB[j], -6.0, fs) / -6.0;
        buildFitMatrix (B);
        logSmoother (A, broadEnvOct, loB, hiB);
        envB.assign (A.nb, 0.0);
        for (int k = 0; k < kBands; ++k)
        {
            const double half = erbBw (bc[k]) / 2.0;
            bandC[k] = bc[k];
            bandS[k] = upperBound (A, bc[k] - half);
            bandE[k] = std::max (bandS[k], lowerBound (A, bc[k] + half));
        }

        for (int c = 0; c < nch; ++c) dl[c].assign (delay, 0.0f);
        trRing.assign (delay, 0.0f);
        lead = std::min (delay - 1, 64 * scale);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0);
        for (int c = 0; c < nch; ++c) std::fill (dl[c].begin(), dl[c].end(), 0.0f);
        std::fill (trRing.begin(), trRing.end(), 0.0f);
        ringPos = dlPos = hopCount = ctrlCount = hopIndex = 0;
        for (Spec* s : { &A, &L })
        {
            s->init = false;
            std::fill (s->occ.begin(), s->occ.end(), 0.0);
            std::fill (s->prot.begin(), s->prot.end(), 0.0);
            std::fill (s->harm.begin(), s->harm.end(), 0.0);
        }
        pitchF0 = 100.0; pitchConf = 0.0; nF0 = 0; nF0L = 0;
        Lp.ready = false;
        envFast = envSlow = transient = 0.0;
        for (int i = 0; i < kNarrow; ++i) { grN[i] = 0; fqN[i] = regC[i]; }
        for (int i = 0; i < kBands; ++i) grB[i] = 0;
        for (int i = 0; i < kNarrow; ++i) { meterGainN[i] = 0.0f; meterFreqN[i] = (float) regC[i]; }
        for (int i = 0; i < kBroad; ++i) meterGainB[i] = 0.0f;
        for (int f = 0; f < kFilters; ++f)
        {
            gStart[f] = gEnd[f] = 0;
            const double fc = f < kNarrow ? regC[f] : fcB[f - kNarrow];
            lfStart[f] = lfEnd[f] = std::log (fc);
            for (int c = 0; c < kMaxCh; ++c) s1[f][c] = s2[f][c] = 0;
        }
        rampPos = 0;
        updateCoeffs();
    }

    int getLatencySamples() const { return delay; }
    double getBroadFc (int j) const { return fcB[j]; }
    double getBroadQ (int j) const { return qB[j]; }
    // magnitude (dB) of one bell at frequency f; the cascade's curve is the sum
    static double bellResponseDb (double f, double fc, double q, double gainDb, double sampleRate)
    {
        const double A_ = std::pow (10.0, gainDb / 40), w0 = 2 * M_PI * fc / sampleRate, al = std::sin (w0) / (2 * q);
        const std::complex<double> z = std::polar (1.0, -2 * M_PI * f / sampleRate);
        const std::complex<double> b = (1 + al * A_) + (-2 * std::cos (w0)) * z + (1 - al * A_) * z * z;
        const std::complex<double> a = (1 + al / A_) + (-2 * std::cos (w0)) * z + (1 - al / A_) * z * z;
        return 20 * std::log10 (std::abs (b / a) + 1e-12);
    }
    // Chosen from the Lab sweep at 48 kHz (attack to 90% on a gated resonance):
    //   Standard  (2048-pt):  1536 samples = 32 ms latency, 29 ms attack
    //   High-res  (4096-pt):  4352 samples = 91 ms latency,  8 ms attack
    static int defaultDelay (bool highRes) { return highRes ? 4352 : 1536; }
    void setParams (const FrankenParams& p) { prm = p; }

    // block-based, in place, any block size
    void process (float* const* ch, int numSamples)
    {
        const double aFa = tc (0.3), aFr = tc (8.0), aSa = tc (40.0), aSr = tc (150.0);
        const double aTa = tc (0.3), aTr = tc (60.0);
        const int R = (int) ring.size();
        for (int i = 0; i < numSamples; ++i)
        {
            double m = 0.0;
            for (int c = 0; c < nch; ++c) m += ch[c][i];
            m /= nch;
            ring[ringPos] = m;
            ringPos = (ringPos + 1) % R;

            // transient detector on the incoming (future) audio
            const double a = std::abs (m);
            envFast = a > envFast ? aFa * envFast + (1 - aFa) * a : aFr * envFast + (1 - aFr) * a;
            envSlow = a > envSlow ? aSa * envSlow + (1 - aSa) * a : aSr * envSlow + (1 - aSr) * a;
            const double r = envFast / (envSlow + 1e-9);
            const float amt = (float) std::clamp ((r - 1.6) / (3.2 - 1.6), 0.0, 1.0);
            const double ahead = trRing[(size_t) ((dlPos + lead) % delay)];
            trRing[(size_t) dlPos] = amt;
            transient = ahead > transient ? aTa * transient + (1 - aTa) * ahead : aTr * transient + (1 - aTr) * ahead;

            if (++hopCount == hop) { hopCount = 0; analyse(); }
            if (++ctrlCount >= kCtrl) { ctrlCount = 0; updateCoeffs(); }
            rampPos = std::min (rampPos + 1, hop);

            float dry[kMaxCh]; double v[kMaxCh];
            for (int c = 0; c < nch; ++c) { dry[c] = dl[c][dlPos]; dl[c][dlPos] = ch[c][i]; v[c] = dry[c]; }
            for (int f = 0; f < kFilters; ++f)
            {
                const double a1 = ca1[f], a2 = ca2[f], a3 = ca3[f], m1 = cm1[f];
                for (int c = 0; c < nch; ++c)
                {
                    const double v3 = v[c] - s2[f][c];
                    const double v1 = a1 * s1[f][c] + a2 * v3;
                    const double v2 = s2[f][c] + a2 * s1[f][c] + a3 * v3;
                    s1[f][c] = 2.0 * v1 - s1[f][c];
                    s2[f][c] = 2.0 * v2 - s2[f][c];
                    v[c] = v[c] + m1 * v1;
                }
            }
            for (int c = 0; c < nch; ++c)
            {
                const float wet = prm.delta ? (float) (dry[c] - v[c]) : (float) v[c];
                ch[c][i] = prm.mix * wet + (1.0f - prm.mix) * dry[c];
            }
            dlPos = (dlPos + 1) % delay;
        }
    }

    // for meters / GUI (written on the audio thread, read anywhere)
    std::atomic<float> meterGainN[kNarrow], meterFreqN[kNarrow], meterGainB[kBroad];

private:
    // one analysed spectrum (the main one and the long low-end one)
    struct Spec
    {
        int n = 0, nb = 0;
        double binHz = 1.0;
        RealFFT fft;
        std::vector<double> win, frame, re, im, Psm, Pdb, env, dev, occ, prot, harm, cs, fbins, binToLg;
        std::vector<int> loS, hiS, loP, hiP;
        std::vector<char> ok;
        int medW = 25;
        bool init = false;
    };

    // ---------------- fixed structure
    const double fLo = 40.0, fHi = 18000.0, envOct = 0.5, broadEnvOct = 2.0, specSmoothMs = 25.0;
    bool useUpperEnv = true;
    const double lowSplit = 300.0, persistTauS = 1.5, guardDb = 12.0, lowEnvOct = 0.5;
    double fs = 48000;
    int lowProtEnd = 0;
    int nch = 2, hop = 256, delay = 2560, medW = 25, lowEvery = 4, lead = 64, lowFactor = 4;
    FrankenParams prm;

    Spec A, L;
    std::vector<double> lg, pg, medG, medWin, scratch, envB, ring;
    std::vector<int> loB, hiB;
    int regS[kNarrow], regE[kNarrow], poolS[kNarrow], poolE[kNarrow];
    int regSL[kNarrow], regEL[kNarrow], poolSL[kNarrow], poolEL[kNarrow];
    bool useL[kNarrow];
    double regC[kNarrow];
    int bandS[kBands], bandE[kBands];
    double bandC[kBands];
    double fcB[kBroad], qB[kBroad], M[kBroad][kBands];

    // ---------------- state
    std::vector<float> dl[kMaxCh], trRing;
    int ringPos = 0, dlPos = 0, hopCount = 0, ctrlCount = 0, rampPos = 0, hopIndex = 0;
    double pitchF0 = 100.0, pitchConf = 0.0;
    double f0s[kMaxPitches], f0sL[kMaxPitches]; int nF0 = 0, nF0L = 0;
    // the finished low spectrum the narrow layer reads (published every lowEvery hops)
    struct LowPublished { std::vector<double> Pdb, dev, prot, harm; bool ready = false; } Lp;
    struct View { const double *Pdb, *dev, *prot, *harm, *fbins; int nb; double binHz; };
    void publishLow()
    {
        Lp.Pdb = L.Pdb; Lp.dev = L.dev; Lp.prot = L.prot; Lp.harm = L.harm;   // same sizes: no allocation
        Lp.ready = true;
    }
    double pkF[kMaxPeaks], pkH[kMaxPeaks]; bool pkUsed[kMaxPeaks];
    double envFast = 0, envSlow = 0, transient = 0;
    double grN[kNarrow], fqN[kNarrow], grB[kBands];
    double gStart[kFilters], gEnd[kFilters], lfStart[kFilters], lfEnd[kFilters];
    double ca1[kFilters], ca2[kFilters], ca3[kFilters], cm1[kFilters];
    double s1[kFilters][kMaxCh], s2[kFilters][kMaxCh];

    // ---------------- helpers
    double tc (double ms) const { return std::exp (-1.0 / (fs * ms / 1000.0)); }     // per-sample
    double coef (double ms, int hops = 1) const { return std::exp (-hop * hops / (fs * ms / 1000.0)); }

    static std::vector<double> erbCenters (int n, double lo, double hi)
    {
        auto e = [] (double f) { return 21.4 * std::log10 (1 + 0.00437 * f); };
        std::vector<double> v (n);
        for (int i = 0; i < n; ++i)
        {
            const double E = e (lo) + (e (hi) - e (lo)) * (n == 1 ? 0.0 : (double) i / (n - 1));
            v[i] = (std::pow (10.0, E / 21.4) - 1) / 0.00437;
        }
        return v;
    }
    static double erbBw (double f) { return 24.7 * (4.37 * f / 1000 + 1); }
    static int lowerBound (const Spec& s, double f) { return (int) (std::lower_bound (s.fbins.begin(), s.fbins.end(), f) - s.fbins.begin()); }
    static int upperBound (const Spec& s, double f) { return (int) (std::upper_bound (s.fbins.begin(), s.fbins.end(), f) - s.fbins.begin()); }
    static void logSmoother (const Spec& s, double w, std::vector<int>& lo, std::vector<int>& hi)
    {
        lo.resize (s.nb); hi.resize (s.nb);
        for (int k = 0; k < s.nb; ++k)
        {
            const double f = std::max (s.fbins[k], 1.0);
            lo[k] = lowerBound (s, f * std::pow (2.0, -w / 2));
            hi[k] = std::max (upperBound (s, f * std::pow (2.0, w / 2)), lo[k] + 1);
            hi[k] = std::min (hi[k], s.nb);
            lo[k] = std::min (lo[k], hi[k] - 1);
        }
    }

    void setupSpec (Spec& s, int n)
    {
        s.n = n; s.nb = n / 2 + 1; s.binHz = fs / n;
        s.fft.setup (n);
        s.win.resize (n);
        for (int i = 0; i < n; ++i) s.win[i] = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / n);   // periodic Hann
        s.fbins.resize (s.nb);
        for (int k = 0; k < s.nb; ++k) s.fbins[k] = k * s.binHz;
        for (auto* v : { &s.frame }) v->assign (n, 0.0);
        for (auto* v : { &s.re, &s.im, &s.Psm, &s.Pdb, &s.env, &s.dev, &s.occ, &s.prot, &s.harm })
            v->assign (s.nb, 0.0);
        s.cs.assign (s.nb + 1, 0.0);
        logSmoother (s, envOct, s.loS, s.hiS);
        logSmoother (s, 1.0 / 24.0, s.loP, s.hiP);        // persistence: +-1/48 octave
        const int nlg = (int) lg.size();
        s.binToLg.resize (s.nb);
        for (int k = 0; k < s.nb; ++k)
        {
            const double f = s.fbins[k];
            if (f <= lg.front()) s.binToLg[k] = 0.0;
            else if (f >= lg.back()) s.binToLg[k] = nlg - 1.0;
            else s.binToLg[k] = std::log2 (f / lg[0]) * 48.0;
        }
        s.ok.assign (s.nb, 0);
        for (int k = 0; k < s.nb; ++k) s.ok[k] = s.fbins[k] > fLo && s.fbins[k] < fHi;
    }

    void buildFitMatrix (const std::vector<double>& B)
    {
        double Am[kBroad][kBroad], R[kBroad][kBands];
        for (int i = 0; i < kBroad; ++i)
        {
            for (int j = 0; j < kBroad; ++j)
            {
                double s = 0; for (int k = 0; k < kBands; ++k) s += B[k * kBroad + i] * B[k * kBroad + j];
                Am[i][j] = s + (i == j ? 1e-2 : 0.0);
            }
            for (int k = 0; k < kBands; ++k) R[i][k] = B[k * kBroad + i];
        }
        for (int c = 0; c < kBroad; ++c)
        {
            int p = c; for (int r = c + 1; r < kBroad; ++r) if (std::abs (Am[r][c]) > std::abs (Am[p][c])) p = r;
            for (int j = 0; j < kBroad; ++j) std::swap (Am[c][j], Am[p][j]);
            for (int k = 0; k < kBands; ++k) std::swap (R[c][k], R[p][k]);
            const double d = Am[c][c];
            for (int j = 0; j < kBroad; ++j) Am[c][j] /= d;
            for (int k = 0; k < kBands; ++k) R[c][k] /= d;
            for (int r = 0; r < kBroad; ++r)
                if (r != c)
                {
                    const double f = Am[r][c];
                    for (int j = 0; j < kBroad; ++j) Am[r][j] -= f * Am[c][j];
                    for (int k = 0; k < kBands; ++k) R[r][k] -= f * R[c][k];
                }
        }
        for (int i = 0; i < kBroad; ++i) for (int k = 0; k < kBands; ++k) M[i][k] = R[i][k];
    }
    static double medianOf (double* v, int n)
    {
        std::nth_element (v, v + n / 2, v + n);
        const double hi = v[n / 2];
        if (n % 2) return hi;
        return 0.5 * (hi + *std::max_element (v, v + n / 2));
    }
    double knee (double e) const
    {
        const double kn = prm.kneeDb;
        if (e > kn / 2) return e;
        if (e > -kn / 2) return (e + kn / 2) * (e + kn / 2) / (2 * kn);
        return 0.0;
    }
    double rangeWeight (double f) const
    {
        f = std::max (f, 1.0);
        const double wl = std::clamp (std::log2 (f / prm.lowHz) * 3.0 + 1.0, 0.0, 1.0);
        const double wh = std::clamp (std::log2 (prm.highHz / f) * 3.0 + 1.0, 0.0, 1.0);
        return wl * wh;
    }
    static double smoothstep (double a, double b, double x)
    {
        const double t = std::clamp ((x - a) / (b - a), 0.0, 1.0);
        return t * t * (3 - 2 * t);
    }

    // ---------------- spectrum + envelope
    void computeSpectrum (Spec& s, double aSp)
    {
        const int R = (int) ring.size();
        const int start = (ringPos - s.n + R) % R;
        for (int i = 0; i < s.n; ++i) s.frame[i] = ring[(start + i) % R] * s.win[i];
        s.fft.forward (s.frame.data(), s.re.data(), s.im.data());
        for (int k = 0; k < s.nb; ++k)
        {
            const double p = s.re[k] * s.re[k] + s.im[k] * s.im[k] + 1e-14;
            s.Psm[k] = s.init ? aSp * s.Psm[k] + (1 - aSp) * p : p;
            s.Pdb[k] = 10 * std::log10 (s.Psm[k]);
        }
        s.init = true;
    }

    void computeEnvelope (Spec& s)
    {
        s.cs[0] = 0; for (int k = 0; k < s.nb; ++k) s.cs[k + 1] = s.cs[k] + s.Psm[k];
        const int nlg = (int) lg.size();
        for (int i = 0; i < nlg; ++i)
        {
            double x = lg[i] / s.binHz; x = std::clamp (x, 0.0, (double) (s.nb - 1));
            const int k0 = std::min ((int) x, s.nb - 2); const double fr = x - k0;
            pg[i] = s.Pdb[k0] * (1 - fr) + s.Pdb[k0 + 1] * fr;
        }
        // running median over the log grid: keep the window sorted and slide it
        // (one removal + one insertion per step instead of a selection per point)
        const int w = s.medW, h = w / 2;
        double* win = medWin.data();
        for (int j = 0; j < w; ++j) win[j] = pg[std::clamp (j - h, 0, nlg - 1)];
        std::sort (win, win + w);
        for (int i = 0; i < nlg; ++i)
        {
            medG[i] = win[h];
            if (i + 1 == nlg) break;
            const double out = pg[std::clamp (i - h, 0, nlg - 1)], in = pg[std::clamp (i - h + w, 0, nlg - 1)];
            if (out == in) continue;
            int p = (int) (std::lower_bound (win, win + w, out) - win);
            if (in > out) { while (p + 1 < w && win[p + 1] < in) { win[p] = win[p + 1]; ++p; } }
            else          { while (p > 0 && win[p - 1] > in)     { win[p] = win[p - 1]; --p; } }
            win[p] = in;
        }
        int nOk = 0;
        for (int k = 0; k < s.nb; ++k)
        {
            const double x = s.binToLg[k]; const int i0 = std::min ((int) x, nlg - 2); const double fr = std::clamp (x - i0, 0.0, 1.0);
            const double med = medG[i0] * (1 - fr) + medG[i0 + 1] * fr;
            const double envPm = 10 * std::log10 ((s.cs[s.hiS[k]] - s.cs[s.loS[k]]) / (s.hiS[k] - s.loS[k]));
            s.env[k] = med;
            if (s.ok[k]) scratch[nOk++] = envPm - med;
        }
        const double bias = nOk ? medianOf (scratch.data(), nOk) : 0.0;
        for (int k = 0; k < s.nb; ++k) { s.env[k] += bias; s.dev[k] = s.Pdb[k] - s.env[k]; }
    }

    // persistence: how much of the recent past each frequency spent as a peak
    void updateOccupancy (Spec& s, int hops)
    {
        const double a = std::exp (-hop * hops / (fs * persistTauS));
        for (int k = 0; k < s.nb; ++k)
            s.occ[k] = a * s.occ[k] + (1 - a) * std::clamp (s.dev[k] / 6.0, 0.0, 1.0);
    }

    // protection 0..1 per bin: partial of a detected note, or a peak that hops around
    void computeProtection (Spec& s, int end = -1)
    {
        if (end < 0 || end > s.nb) end = s.nb;
        for (int k = 0; k < end; ++k)
        {
            double occMax = 0.0;
            for (int j = s.loP[k]; j < s.hiP[k]; ++j) occMax = std::max (occMax, s.occ[j]);
            const double novelty = 0.6 * (1.0 - smoothstep (0.10, 0.40, occMax));
            double harm = 0.0;
            const double f = s.fbins[k];
            auto addHarm = [&] (const double* list, int count)
            {
                for (int p = 0; p < count; ++p)
                {
                    const double h = std::round (f / list[p]);
                    if (h < 1 || h > 16) continue;
                    const double tol = std::max (1.5 * s.binHz, 0.006 * h * list[p]);
                    const double d = (f - h * list[p]) / tol;
                    if (d * d < 40.0) harm = std::max (harm, std::exp (-d * d));   // exp(-40) < 1e-17
                }
            };
            addHarm (f0s, nF0);
            addHarm (f0sL, nF0L);
            s.harm[k] = harm;
            s.prot[k] = std::max (novelty, harm);
        }
    }

    // multi-pitch: harmonic summation with estimate-and-cancel (Klapuri-style, simplified)
    void detectPitches (const Spec& s, double f0Lo, double f0Hi, double fMaxPeak, double* out, int& nOut)
    {
        int np = 0;
        for (int k = 2; k < s.nb - 1 && np < kMaxPeaks; ++k)
        {
            const double f = s.fbins[k];
            if (f < 0.9 * f0Lo) continue;
            if (f > fMaxPeak) break;
            if (s.dev[k] >= 6.0 && s.Pdb[k] > s.Pdb[k - 1] && s.Pdb[k] >= s.Pdb[k + 1])
            {
                const double a = s.Pdb[k - 1], b = s.Pdb[k], c = s.Pdb[k + 1];
                const double d = std::clamp (0.5 * (a - c) / (a - 2 * b + c + 1e-12), -0.5, 0.5);
                pkF[np] = (k + d) * s.binHz; pkH[np] = std::min (s.dev[k], 24.0); pkUsed[np] = false; ++np;
            }
        }
        nOut = 0;
        for (int it = 0; it < kMaxPitches; ++it)
        {
            double bestS = 0.0, bestF = 0.0;
            for (double f0 = f0Lo; f0 < f0Hi; f0 *= 1.0145)         // 1/48 octave grid
            {
                double S = 0.0; int nh = 0; bool low = false;
                for (int h = 1; h <= 10; ++h)
                {
                    const double t = h * f0;
                    if (t > fMaxPeak) break;
                    const double tol = std::max (1.5 * s.binHz, 0.02 * t);
                    double best = 0.0;
                    for (int p = (int) (std::lower_bound (pkF, pkF + np, t - tol) - pkF); p < np && pkF[p] <= t + tol; ++p)
                        if (! pkUsed[p]) best = std::max (best, pkH[p]);
                    if (best > 0.0) { S += best / std::sqrt ((double) h); ++nh; if (h <= 2) low = true; }
                }
                if (nh >= 3 && low && S > bestS) { bestS = S; bestF = f0; }
            }
            if (bestS < 20.0) break;
            // refine f0 as the height-weighted median of (partial / h) over the matched
            // partials (robust to a partial borrowed from another note), then cancel them
            double est[32], wt[32]; int ne = 0;
            for (int h = 1; h <= 10; ++h)
            {
                const double t = h * bestF, tol = std::max (1.5 * s.binHz, 0.02 * t);
                for (int p = (int) (std::lower_bound (pkF, pkF + np, t - tol) - pkF); p < np && pkF[p] <= t + tol; ++p)
                    if (! pkUsed[p])
                    {
                        if (ne < 32) { est[ne] = pkF[p] / h; wt[ne] = pkH[p]; ++ne; }
                        pkUsed[p] = true;
                    }
            }
            double f0 = bestF;
            if (ne > 0)
            {
                for (int a = 1; a < ne; ++a)                    // insertion sort by estimate
                    for (int b = a; b > 0 && est[b] < est[b - 1]; --b) { std::swap (est[b], est[b - 1]); std::swap (wt[b], wt[b - 1]); }
                double total = 0; for (int a = 0; a < ne; ++a) total += wt[a];
                double acc = 0;
                for (int a = 0; a < ne; ++a) { acc += wt[a]; if (acc >= 0.5 * total) { f0 = est[a]; break; } }
            }
            out[nOut++] = f0;
        }
    }

    // single-pitch cepstrum (dense harmonic tones) + its upper envelope
    void detectCepstralPitch (Spec& s, double& f0, double& conf)
    {
        for (int k = 0; k < s.nb; ++k) scratch[k] = std::log (std::sqrt (s.re[k] * s.re[k] + s.im[k] * s.im[k]) + 1e-12);
        for (int k = 0; k < s.n; ++k) s.frame[k] = scratch[k < s.nb ? k : s.n - k];
        s.fft.forward (s.frame.data(), s.re.data(), s.im.data());     // real & even -> forward = N * inverse
        const int qmin = (int) (fs / 1000.0), qmax = std::min ((int) (fs / 60.0), s.nb - 1);
        const int n = qmax - qmin;
        int best = qmin; double sum = 0, sum2 = 0;
        for (int q = qmin; q < qmax; ++q)
        {
            const double v = s.re[q] / s.n;
            if (v > s.re[best] / s.n) best = q;
            sum += v; sum2 += v * v;
        }
        const double mean = sum / n, sd = std::sqrt (std::max (sum2 / n - mean * mean, 0.0));
        for (int q = 0; q < n; ++q) scratch[q] = s.re[qmin + q] / s.n;
        const double med = medianOf (scratch.data(), n);
        const double prom = (s.re[best] / s.n - med) / (sd + 1e-12);
        conf = std::clamp ((prom - 4.0) / 4.0, 0.0, 1.0);
        f0 = fs / best;
    }

    void applyUpperEnvelope (Spec& s, double f0, double w)
    {
        const int width = std::max (3, (int) std::lround (1.5 * f0 / s.binHz));
        const int left = width / 2;
        for (int k = 0; k < s.nb; ++k)
        {
            double m = -1e300;
            for (int j = 0; j < width; ++j) m = std::max (m, s.Pdb[std::clamp (k - left + j, 0, s.nb - 1)]);
            scratch[k] = m;
        }
        s.cs[0] = 0; for (int k = 0; k < s.nb; ++k) s.cs[k + 1] = s.cs[k] + scratch[k];
        for (int k = 0; k < s.nb; ++k)
        {
            const int a = k - left, b = a + width;
            double sum = 0;
            if (a >= 0 && b <= s.nb) sum = s.cs[b] - s.cs[a];
            else for (int j = a; j < b; ++j) sum += scratch[std::clamp (j, 0, s.nb - 1)];
            const double up = sum / width;
            s.env[k] = (1 - w) * s.env[k] + w * std::max (s.env[k], up);
            s.dev[k] = s.Pdb[k] - s.env[k];
        }
    }

    // ---------------- per-hop analysis
    void analyse()
    {
        const double aSp = coef (specSmoothMs);
        computeSpectrum (A, aSp);
        double pmax = -1e9;
        for (int k = 0; k < A.nb; ++k) pmax = std::max (pmax, A.Pdb[k]);
        // The long low-end spectrum is 4x the work of the main one. Instead of doing it all
        // on one hop (a CPU spike every 4th hop that small host buffers cannot absorb) its
        // stages run on successive hops and the finished result is published on the last.
        const int lowPhase = hopIndex % lowEvery;
        const bool doPitch = (hopIndex & 1) == 0;
        ++hopIndex;

        double tgtN[kNarrow] = {}, fNew[kNarrow], tgtB[kBands] = {};
        for (int i = 0; i < kNarrow; ++i) fNew[i] = fqN[i];
        const double slope = 1.0 - 1.0 / prm.ratio;
        const double guard = prm.tonalGuard;

        if (pmax >= -100)
        {
            computeEnvelope (A);
            updateOccupancy (A, 1);
            // low spectrum, one stage per hop: transform, envelope, then notes + protection
            if (lowPhase == 1) computeSpectrum (L, coef (specSmoothMs, lowEvery));
            if (lowPhase == 1 && L.init) { computeEnvelope (L); updateOccupancy (L, lowEvery); }
            if (guard > 0)
            {
                if (doPitch)
                {
                    detectPitches (A, 55.0, 1100.0, 6000.0, f0s, nF0);   // uses dev before the upper envelope
                    detectCepstralPitch (A, pitchF0, pitchConf);       // overwrites A.re/A.im (not needed after)
                }
                if (useUpperEnv && pitchConf > 0) applyUpperEnvelope (A, pitchF0, guard * pitchConf);
                if (! doPitch) computeProtection (A);        // protection moves slowly (1.5 s persistence)
            }
            else
            {
                std::fill (A.prot.begin(), A.prot.end(), 0.0);
                std::fill (A.harm.begin(), A.harm.end(), 0.0);
            }
            if (lowPhase == 2 && L.init)
            {
                if (guard > 0)
                {
                    detectPitches (L, 30.0, 400.0, 2000.0, f0sL, nF0L);   // bass notes
                    computeProtection (L, lowProtEnd);   // only the bins the low regions read
                }
                else
                {
                    std::fill (L.prot.begin(), L.prot.end(), 0.0);
                    std::fill (L.harm.begin(), L.harm.end(), 0.0);
                }
                publishLow();
            }

            // narrow layer
            for (int i = 0; i < kNarrow; ++i)
            {
                const View s = useL[i] ? View { Lp.Pdb.data(), Lp.dev.data(), Lp.prot.data(), Lp.harm.data(), L.fbins.data(), L.nb, L.binHz }
                                       : View { A.Pdb.data(), A.dev.data(), A.prot.data(), A.harm.data(), A.fbins.data(), A.nb, A.binHz };
                if (useL[i] && ! Lp.ready) continue;
                const int rs = useL[i] ? regSL[i] : regS[i], re_ = useL[i] ? regEL[i] : regE[i];
                const int ps = useL[i] ? poolSL[i] : poolS[i], pe = useL[i] ? poolEL[i] : poolE[i];
                if (re_ <= rs) continue;
                // noise scatter from bins that are not protected musical partials,
                // so a chord's own lines do not raise the bar for a real resonance
                int n = 0;
                for (int k = ps; k < pe; ++k) if (s.harm[k] < 0.5) scratch[n++] = s.dev[k];
                if (n < 5) { n = 0; for (int k = ps; k < pe; ++k) scratch[n++] = s.dev[k]; }
                const double md = medianOf (scratch.data(), n);
                int n2 = 0;
                for (int k = ps; k < pe; ++k) if (n == pe - ps || s.harm[k] < 0.5) scratch[n2++] = std::abs (s.dev[k] - md);
                const double mad = 1.4826 * medianOf (scratch.data(), n2);
                // strongest peak after protection
                int j = rs; double best = -1e9;
                for (int k = rs; k < re_; ++k)
                {
                    const double eff = s.dev[k] - guard * guardDb * s.prot[k];
                    if (eff > best) { best = eff; j = k; }
                }
                const double e = best - prm.sensitivityDb - prm.scatterK * std::min (mad, 6.0);
                if (e <= -prm.kneeDb / 2) continue;
                if (j > 0 && j < s.nb - 1)
                {
                    const double a = s.Pdb[j - 1], b = s.Pdb[j], c = s.Pdb[j + 1];
                    const double d = 0.5 * (a - c) / (a - 2 * b + c + 1e-12);
                    fNew[i] = (j + std::clamp (d, -0.5, 0.5)) * s.binHz;
                }
                else fNew[i] = s.fbins[j];
                // a partial of a detected note is protected in proportion, however loud it is
                const double cut = -knee (e) * slope * prm.depth * rangeWeight (fNew[i]) * (1.0 - guard * s.harm[j]);
                tgtN[i] = std::max (cut, (double) -prm.maxCutDb);
            }

            // broad layer (main spectrum)
            A.cs[0] = 0; for (int k = 0; k < A.nb; ++k) A.cs[k + 1] = A.cs[k] + A.Psm[k];
            for (int k = 0; k < A.nb; ++k) envB[k] = 10 * std::log10 ((A.cs[hiB[k]] - A.cs[loB[k]]) / (hiB[k] - loB[k]));
            for (int b = 0; b < kBands; ++b)
            {
                if (bandE[b] <= bandS[b]) continue;
                double Lsum = 0, Esum = 0, pw = 0, pp = 0;
                for (int k = bandS[b]; k < bandE[b]; ++k) { Lsum += A.Pdb[k]; Esum += envB[k]; pw += A.Psm[k]; pp += A.Psm[k] * A.prot[k]; }
                const int n = bandE[b] - bandS[b];
                const double protB = pw > 0 ? pp / pw : 0.0;
                tgtB[b] = -std::max (0.0, Lsum / n - Esum / n - prm.sensitivityDb) * slope * prm.broadDepth
                          * rangeWeight (bandC[b]) * (1.0 - guard * protB);
            }
        }

        // dynamics
        const double aAtt = coef (prm.attackMs), aRel = coef (prm.releaseMs), aGl = coef (prm.glideMs);
        for (int i = 0; i < kNarrow; ++i)
        {
            // a bell may not move faster than ~2 cycles of its own frequency: changing a
            // low cut inside a single bass cycle reshapes the wave and raises its peaks
            const double cyc = 1000.0 / std::max (fqN[i], 20.0);              // ms per cycle
            const double att = cyc * 2.0 > prm.attackMs ? coef (cyc * 2.0) : aAtt;
            const double rel = cyc * 4.0 > prm.releaseMs ? coef (cyc * 4.0) : aRel;
            const double gl = cyc * 3.0 > prm.glideMs ? coef (cyc * 3.0) : aGl;
            const double c = tgtN[i] < grN[i] ? att : rel;
            grN[i] = c * grN[i] + (1 - c) * tgtN[i];
            if (tgtN[i] < -0.05) fqN[i] = std::exp (gl * std::log (fqN[i]) + (1 - gl) * std::log (fNew[i]));
        }
        for (int b = 0; b < kBands; ++b)
        {
            const double c = tgtB[b] < grB[b] ? aAtt : aRel;
            grB[b] = c * grB[b] + (1 - c) * tgtB[b];
        }

        // new ramp targets: start where the current ramp has got to
        const double t = (double) rampPos / hop;
        for (int f = 0; f < kFilters; ++f)
        {
            gStart[f] = gStart[f] + (gEnd[f] - gStart[f]) * t;
            lfStart[f] = lfStart[f] + (lfEnd[f] - lfStart[f]) * t;
        }
        const double fcMax = fs * 0.45;
        for (int i = 0; i < kNarrow; ++i)
        {
            gEnd[i] = grN[i];
            lfEnd[i] = std::log (std::clamp (fqN[i], 20.0, fcMax));
            meterGainN[i].store ((float) grN[i], std::memory_order_relaxed);
            meterFreqN[i].store ((float) fqN[i], std::memory_order_relaxed);
        }
        for (int j = 0; j < kBroad; ++j)
        {
            double s = 0; for (int b = 0; b < kBands; ++b) s += M[j][b] * grB[b];
            gEnd[kNarrow + j] = std::clamp (s, (double) -prm.maxCutDb, 1.0);
            meterGainB[j].store ((float) gEnd[kNarrow + j], std::memory_order_relaxed);
        }
        rampPos = 0;
    }

    void updateCoeffs()
    {
        const double t = std::min (1.0, (rampPos + kCtrl * 0.5) / hop);
        const double ease = 1.0 - prm.transientGuard * transient;      // drum hits keep their snap
        for (int f = 0; f < kFilters; ++f)
        {
            const double fc = std::exp (lfStart[f] + (lfEnd[f] - lfStart[f]) * t);
            // ease only above ~250 Hz: drum snap lives there, and quickly releasing a
            // narrow low cut on a kick shifts its phase and raises the peak
            const double e = ease + (1.0 - ease) * (1.0 - smoothstep (180.0, 320.0, fc));
            const double gd = (gStart[f] + (gEnd[f] - gStart[f]) * t) * e;
            const double q = f < kNarrow ? prm.sharpness : qB[f - kNarrow];
            const double g = std::tan (M_PI * fc / fs), A_ = std::pow (10.0, gd / 40.0), k = 1.0 / (q * A_);
            ca1[f] = 1.0 / (1.0 + g * (g + k)); ca2[f] = g * ca1[f]; ca3[f] = g * ca2[f];
            cm1[f] = k * (A_ * A_ - 1.0);
        }
    }
};

} // namespace muzi

#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic pop
#endif
