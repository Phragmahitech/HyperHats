#pragma once

/*
    HyperHats — moteur de synthèse de charleston (sans JUCE, testable seul).

    Une voix = une frappe. Le moteur est monophonique : chaque frappe coupe la
    précédente en 3 ms (le closed coupe l'open, comme une vraie charley).

    Chaîne d'une frappe :
        métal (SQUARE / FM / RING, fondu vers un banc de partiels « acoustique »
        selon CHARACTER) + bruit (dosés par SOURCE)
        -> passe-haut à 0,45 x COLOR -> filtre d'état à COLOR (passe-bande + aigus)
        -> enveloppe (CLOSED ou OPEN, forme naturelle -> gatée selon SHAPE)
        + transitoire (SNAP), « chick » du closed et « wash » de l'open (CHARACTER)
    Puis, sur la somme : CRUSH (échantillonnage et résolution), LEVEL, écrêtage doux.

    Les réglages de timbre (alliage, source, character, tune, decay, shape, snap,
    variation) sont figés au moment de la frappe ; COLOR, WIDTH, CRUSH et LEVEL
    agissent en continu.
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace hyperhats
{

constexpr double pi = 3.14159265358979323846;

enum Alloy { alloySquare = 0, alloyFm = 1, alloyRing = 2 };

struct HatParams
{
    int   alloy     = alloyFm;
    float source    = 0.5f;     // 0 = métal, 1 = bruit
    float character = 0.35f;    // 0 = synthétique, 1 = acoustique
    float tune      = 0.0f;     // demi-tons, -12 à +12
    float colorHz   = 9500.0f;  // centre du filtre, 2 à 16 kHz
    float snap      = 0.75f;    // transitoire, 0 à 1
    float closedMs  = 20.0f;    // 10 à 300 ms
    float openMs    = 300.0f;   // 80 à 2500 ms
    float shape     = 0.7f;     // 0 = naturel, 1 = gaté
    float width     = 0.35f;    // 0 = mono, 1 = bruit décorrélé à gauche et à droite
    float variation = 0.25f;    // différences d'une frappe à l'autre
    float crush     = 0.1f;     // 0 = propre, 1 = 4 bits et échantillonnage divisé par 16
    float levelDb   = 0.0f;
};

//==============================================================================
/** Générateur de bruit (xorshift32), valeurs dans [-1, 1). */
struct Noise
{
    uint32_t state = 0x12345678u;

    void seed (uint32_t s) noexcept        { state = s != 0 ? s : 0x9e3779b9u; }

    uint32_t nextInt() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float next() noexcept                  { return (float) ((double) nextInt() * (2.0 / 4294967296.0) - 1.0); }
    float unit() noexcept                  { return (float) ((double) nextInt() * (1.0 / 4294967296.0)); }
};

//==============================================================================
/** Filtre d'état (forme TPT de Zavalishin). */
struct Svf
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1.4142f;
    float ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;

    void setup (double cutoff, double q, double sampleRate) noexcept
    {
        const double fc = std::min (cutoff, sampleRate * 0.45);
        const double g = std::tan (pi * fc / sampleRate);
        k = (float) (1.0 / q);
        const double d = 1.0 / (1.0 + g * (g + k));
        a1 = (float) d;
        a2 = (float) (g * d);
        a3 = (float) (g * g * d);
    }

    void clear() noexcept                  { ic1 = ic2 = 0; }

    void process (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
};

//==============================================================================
/** Carré à bande limitée (polyBLEP). */
struct BlepSquare
{
    double phase = 0, inc = 0;

    static double blep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0;
        }

        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }

        return 0.0;
    }

    float next() noexcept
    {
        double v = phase < 0.5 ? 1.0 : -1.0;
        v += blep (phase, inc);
        double t2 = phase + 0.5;
        if (t2 >= 1.0) t2 -= 1.0;
        v -= blep (t2, inc);

        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        return (float) v;
    }
};

//==============================================================================
/** Une frappe. */
class HatVoice
{
public:
    static constexpr int maxPartials = 48;

    bool isActive() const noexcept         { return active; }
    bool isFading() const noexcept         { return fading; }
    bool isOpen() const noexcept           { return open; }

    /** Coupe la frappe en 3 ms. */
    void choke (double rate) noexcept
    {
        if (active && ! fading)
        {
            fading = true;
            fadeGain = 1.0f;
            fadeStep = 1.0f / (float) std::max (1.0, 0.003 * rate);
        }
    }

    void kill() noexcept                   { active = false; fading = false; }

    void start (const HatParams& p, bool isOpenHit, float velocity, double rate, uint32_t hitSeed) noexcept
    {
        const double sr = rate;
        sampleRate = sr;
        open = isOpenHit;
        active = true;
        fading = false;
        fadeGain = 1.0f;
        t = 0;

        Noise dice;
        dice.seed (hitSeed * 2654435761u + 17u);
        auto signedDie = [&dice] { return dice.next(); };

        const float c = clamp01 (p.character);
        const float vary = clamp01 (p.variation);   // VARIATION seul : à 0, toutes les frappes sont identiques

        // Réglages de la frappe, avec ses petites différences
        const double tuneSt = p.tune + vary * 0.30 * signedDie();                         // jusqu'à 30 centièmes
        const double ratio = std::pow (2.0, tuneSt / 12.0);
        colorMul = (float) ((1.0 + vary * 0.18 * signedDie()) * (0.72 + 0.28 * velocity));
        const double decayMs = (isOpenHit ? p.openMs : p.closedMs) * (1.0 + vary * 0.18 * signedDie());
        hitGain = (float) ((0.12 + 0.88 * std::pow (std::max (0.0f, velocity), 1.3f)) * (1.0 + vary * 0.22 * signedDie()));
        const float pan = juce_like_clamp (vary * 0.30f * signedDie() * clamp01 (p.width), -0.5f, 0.5f);   // WIDTH à 0 : mono strict
        panL = (float) std::cos ((pan + 1.0) * pi * 0.25) * 1.41421356f;
        panR = (float) std::sin ((pan + 1.0) * pi * 0.25) * 1.41421356f;

        alloy = std::clamp (p.alloy, 0, 2);
        source = clamp01 (p.source);
        modalAmount = c * c * (3.0f - 2.0f * c);          // fondu doux synth -> acoustique
        snap = clamp01 (p.snap) * (0.4f + 0.6f * velocity);
        chickAmount = isOpenHit ? 0.0f : 0.55f * c;
        washAmount = isOpenHit ? 0.5f * c : 0.0f;
        shape = clamp01 (p.shape);

        // Enveloppe : naturelle = exp(-4 t / D), gatée = 1 - (t / D)^6 jusqu'à D
        decaySamples = std::max (8.0, decayMs * 0.001 * sr);
        expMul = std::exp (-4.0 / decaySamples);
        expNow = 1.0;
        gateStep = 1.0 / decaySamples;
        gatePos = 0;
        attack = 0;
        attackStep = 1.0f / (float) std::max (1.0, 0.00012 * sr);    // 0,12 ms : évite le clic numérique, garde le claquement

        transientMul = (float) std::exp (-1.0 / (0.00045 * sr));
        transientEnv = 1.0f;
        chickMul = (float) std::exp (-1.0 / (0.003 * sr));
        chickEnv = 1.0f;
        washRise = (float) std::exp (-1.0 / (0.022 * sr));
        washEnv = 0.0f;
        fmIndexMul = (float) std::exp (-1.0 / (0.008 * sr));
        fmIndexEnv = 1.0f;

        // Bruits : un par côté (largeur stéréo), graines de la frappe
        noiseA.seed (hitSeed * 747796405u + 1u);
        noiseB.seed (hitSeed * 2891336453u + 7u);
        noiseC.seed (hitSeed * 1103515245u + 12345u);

        // Métal synthétique
        static constexpr double squareHz[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };   // les six oscillateurs de la TR-808
        static constexpr double ringHz[6]   = { 330.0, 442.9, 564.6, 749.1, 950.4, 1296.9 };
        static constexpr double fmCarrier[3] = { 1.0, 1.53, 2.31 };
        static constexpr double fmMod[3]     = { 1.79, 3.11, 4.37 };
        const double fmBase = 1100.0;

        for (int i = 0; i < 6; ++i)
        {
            squares[i].inc = std::min (0.45, squareHz[i] * ratio / sr);
            squares[i].phase = dice.unit();
            rings[i].inc = std::min (0.45, ringHz[i] * ratio / sr);
            rings[i].phase = dice.unit();
        }

        for (int i = 0; i < 3; ++i)
        {
            fmCarrierInc[i] = fmCarrier[i] * fmBase * ratio / sr;
            fmModInc[i] = fmMod[i] * fmBase * ratio / sr;
            fmCarrierPhase[i] = dice.unit();
            fmModPhase[i] = dice.unit();
        }

        // Banc de partiels « acoustique » : fréquences propres à l'alliage, phases propres à la frappe
        Noise layout;
        layout.seed (0xC0FFEEu + (uint32_t) alloy * 977u);
        const double nyquistGuard = sr * 0.45;
        const double partialCount = 6.0 + 42.0 * c;
        numPartials = 0;
        double norm = 0.0;

        for (int i = 0; i < maxPartials; ++i)
        {
            const double u = layout.unit();
            const double f = 450.0 * std::pow (15000.0 / 450.0, std::pow (u, 0.7)) * ratio * (1.0 + vary * 0.004 * signedDie());
            const double weightInLayer = std::clamp (partialCount - i, 0.0, 1.0);
            const double a = (0.5 + 0.5 * layout.unit()) * std::pow (f / 1000.0, 0.15) * weightInLayer;
            const double relDecay = std::pow (f / 3000.0, 0.35) * (0.6 + 0.8 * layout.unit());   // les aigus s'éteignent plus vite

            if (f >= nyquistGuard || a <= 0.0)
                continue;

            auto& pt = partials[numPartials++];
            const double w = 2.0 * pi * f / sr;
            pt.rotCos = std::cos (w);
            pt.rotSin = std::sin (w);
            const double ph = 2.0 * pi * dice.unit();
            pt.c = std::cos (ph);
            pt.s = std::sin (ph);
            pt.amp = a;
            pt.extraMul = std::exp (-relDecay * 1.5 / decaySamples);
            pt.extra = 1.0;   // l'enveloppe du partiel repart de 1 à chaque frappe
            norm += a * a;
        }

        partialNorm = norm > 0.0 ? (float) (1.0 / std::sqrt (norm)) : 0.0f;

        // Filtres
        for (auto& f : hpL) f.clear();
        for (auto& f : bpL) f.clear();
        for (auto& f : hpR) f.clear();
        for (auto& f : bpR) f.clear();
        chickL.clear(); chickR.clear();
        snapHpL = snapHpR = snapPrevL = snapPrevR = 0.0f;
        chickL.setup (2300.0 * ratio, 2.0, sr);
        chickR.setup (2300.0 * ratio, 2.0, sr);
        filterCountdown = 0;
        lastColor = -1.0f;
    }

    /** Ajoute la frappe à out[0..n). colorHz et width sont les valeurs courantes (lissées). */
    void render (float* outL, float* outR, int n, float colorHz, float width) noexcept
    {
        if (! active)
            return;

        // Largeur : bruit gauche/droite = cos(a) A ± sin(a) B, puissance constante
        const float angle = clamp01 (width) * (float) (pi * 0.25);
        const float wc = std::cos (angle), ws = std::sin (angle);
        const bool stereoNoise = ws > 1.0e-4f;

        const float snapCoef = (float) std::exp (-2.0 * pi * 2000.0 / sampleRate);

        for (int i = 0; i < n; ++i)
        {
            if (--filterCountdown <= 0)
            {
                updateFilters (colorHz);
                filterCountdown = 32;
            }

            // Enveloppe
            const double gx = gatePos;
            double g6 = gx * gx * gx;
            g6 *= g6;
            const double gate = gx < 1.0 ? 1.0 - g6 : 0.0;
            const float env = (float) ((1.0 - shape) * expNow + shape * gate) * attack;
            expNow *= expMul;
            gatePos += gateStep;
            attack = std::min (1.0f, attack + attackStep);

            // Métal
            const float metal = metalSample();

            // Bruit
            const float nA = noiseA.next();
            const float nB = stereoNoise ? noiseB.next() : 0.0f;
            const float noiseLeft = stereoNoise ? wc * nA + ws * nB : nA;
            const float noiseRight = stereoNoise ? wc * nA - ws * nB : nA;

            washEnv = washRise * washEnv + (1.0f - washRise);
            const float noiseGain = source * kNoiseGain * (1.0f + washAmount * washEnv);
            const float metalGain = (1.0f - source) * metalComp;

            const float inL = metalGain * metal + noiseGain * noiseLeft;
            const float inR = metalGain * metal + noiseGain * noiseRight;

            float yL = filter (inL, hpL[0], bpL[0]);
            float yR = filter (inR, hpR[0], bpR[0]);

            // Transitoire (SNAP) : bruit très court passé au passe-haut ~2 kHz
            float tL = 0, tR = 0;
            if (transientEnv > 1.0e-4f)
            {
                const float burst = snap * kSnapGain * transientEnv;
                const float raw = noiseC.next() * burst;
                tL = raw - snapPrevL + snapCoef * snapHpL;   // passe-haut du 1er ordre
                snapPrevL = raw;
                snapHpL = tL;
                tR = tL;
                transientEnv *= transientMul;
            }

            // « Chick » du closed (CHARACTER) : bruit médium très court
            float cL = 0, cR = 0;
            if (chickAmount > 0 && chickEnv > 1.0e-4f)
            {
                chickL.process (noiseLeft);
                chickR.process (noiseRight);
                cL = chickL.bp * chickAmount * kChickGain * chickEnv;
                cR = chickR.bp * chickAmount * kChickGain * chickEnv;
                chickEnv *= chickMul;
            }

            float sL = (yL * env + tL + cL) * hitGain;
            float sR = (yR * env + tR + cR) * hitGain;

            if (fading)
            {
                sL *= fadeGain;
                sR *= fadeGain;
                fadeGain -= fadeStep;

                if (fadeGain <= 0.0f)
                {
                    active = false;
                    fading = false;
                    return;
                }
            }

            outL[i] += sL * panL;
            outR[i] += sR * panR;

            ++t;

            // Fin : enveloppe sous -90 dB et transitoires terminés
            if (env < 3.0e-5f && attack >= 1.0f && transientEnv < 1.0e-4f && (chickAmount <= 0 || chickEnv < 1.0e-4f) && gatePos > 1.0)
            {
                active = false;
                return;
            }
        }
    }

    // Gains internes (calibrés par Tests/EngineTests.cpp : métal et bruit au même niveau après filtre)
    static constexpr float kSquareGain = 6.40f;
    static constexpr float kFmGain     = 1.35f;
    static constexpr float kRingGain   = 2.50f;
    static constexpr float kModalGain  = 0.65f;
    static constexpr float kNoiseGain  = 0.89f;
    static constexpr float kSnapGain   = 1.30f;
    static constexpr float kChickGain  = 2.20f;

private:
    struct Partial
    {
        double rotCos = 1, rotSin = 0, c = 1, s = 0, amp = 0, extraMul = 1, extra = 1;
    };

    static float clamp01 (float x) noexcept                     { return std::clamp (x, 0.0f, 1.0f); }
    static float juce_like_clamp (float x, float a, float b)    { return std::clamp (x, a, b); }

    void updateFilters (float colorHz) noexcept
    {
        const float fc = std::clamp (colorHz * colorMul, 1000.0f, 20000.0f);

        if (std::abs (fc - lastColor) < 0.5f)
            return;

        lastColor = fc;

        // Le métal synthétique a son énergie dans le médium : plus COLOR monte, plus le filtre en enlève.
        // On compense pour que métal et bruit restent au même niveau sur toute la course de COLOR.
        const float alpha = alloy == alloyFm ? 0.4f : 0.8f;
        const float ratio = fc / 9500.0f;
        metalComp = std::pow (ratio, alpha) * (1.0f - modalAmount) + std::pow (ratio, 0.3f) * modalAmount;

        hpL[0].setup (fc * 0.45, 0.7071, sampleRate);
        hpR[0].setup (fc * 0.45, 0.7071, sampleRate);
        bpL[0].setup (fc, 1.2, sampleRate);
        bpR[0].setup (fc, 1.2, sampleRate);
    }

    static float filter (float x, Svf& hp, Svf& bp) noexcept
    {
        hp.process (x);
        bp.process (hp.hp);
        return 0.35f * bp.hp + 1.4f * bp.bp;   // bande autour de COLOR, un peu d'air au-dessus
    }

    float metalSample() noexcept
    {
        float synth = 0.0f;

        if (modalAmount < 0.999f)
        {
            switch (alloy)
            {
                case alloySquare:
                {
                    float sum = 0;
                    for (auto& o : squares) sum += o.next();
                    synth = sum * (kSquareGain / 6.0f);
                    break;
                }

                case alloyRing:
                {
                    float sum = 0;
                    for (int i = 0; i < 6; i += 2)
                        sum += rings[i].next() * rings[i + 1].next();
                    synth = sum * (kRingGain / 3.0f);
                    break;
                }

                default:
                {
                    const double index = 2.2 + 3.0 * fmIndexEnv;
                    fmIndexEnv *= fmIndexMul;
                    double sum = 0;

                    for (int i = 0; i < 3; ++i)
                    {
                        const double m = std::sin (2.0 * pi * fmModPhase[i]);
                        sum += std::sin (2.0 * pi * fmCarrierPhase[i] + index * m);
                        fmModPhase[i] += fmModInc[i];
                        if (fmModPhase[i] >= 1.0) fmModPhase[i] -= 1.0;
                        fmCarrierPhase[i] += fmCarrierInc[i];
                        if (fmCarrierPhase[i] >= 1.0) fmCarrierPhase[i] -= 1.0;
                    }

                    synth = (float) sum * (kFmGain / 3.0f);
                    break;
                }
            }
        }

        float modal = 0.0f;

        if (modalAmount > 0.001f && numPartials > 0)
        {
            double sum = 0;

            for (int i = 0; i < numPartials; ++i)
            {
                auto& p = partials[i];
                const double c = p.c * p.rotCos - p.s * p.rotSin;
                const double s = p.c * p.rotSin + p.s * p.rotCos;
                p.c = c;
                p.s = s;
                sum += s * p.amp * p.extra;
                p.extra *= p.extraMul;
            }

            modal = (float) sum * partialNorm * kModalGain;

            // Maintien de l'amplitude des oscillateurs récursifs
            if ((t & 1023) == 0)
                for (int i = 0; i < numPartials; ++i)
                {
                    auto& p = partials[i];
                    const double r = 1.0 / std::sqrt (p.c * p.c + p.s * p.s);
                    p.c *= r;
                    p.s *= r;
                }
        }

        return synth * (1.0f - modalAmount) + modal * modalAmount;
    }

    double sampleRate = 48000.0;
    bool active = false, fading = false, open = false;
    float fadeGain = 1, fadeStep = 0;
    uint32_t t = 0;

    int alloy = alloyFm;
    float source = 0.5f, modalAmount = 0, snap = 0, chickAmount = 0, washAmount = 0, shape = 0;
    float hitGain = 1, colorMul = 1, panL = 1, panR = 1, metalComp = 1;

    double decaySamples = 1000, expMul = 1, expNow = 1, gateStep = 0, gatePos = 0;
    float attack = 0, attackStep = 1;
    float transientMul = 0, transientEnv = 0, chickMul = 0, chickEnv = 0, washRise = 0, washEnv = 0;
    float fmIndexMul = 0, fmIndexEnv = 0;

    Noise noiseA, noiseB, noiseC;
    BlepSquare squares[6], rings[6];
    double fmCarrierInc[3] {}, fmModInc[3] {}, fmCarrierPhase[3] {}, fmModPhase[3] {};
    Partial partials[maxPartials];
    int numPartials = 0;
    float partialNorm = 0;

    Svf hpL[1], bpL[1], hpR[1], bpR[1], chickL, chickR;
    float snapHpL = 0, snapHpR = 0, snapPrevL = 0, snapPrevR = 0;
    int filterCountdown = 0;
    float lastColor = -1.0f;
};

//==============================================================================
class HatEngine
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
        snapSmoothers();
    }

    void reset() noexcept
    {
        for (auto& v : voices)
            v.kill();

        holdPhase = 0;
        heldL = heldR = 0;
    }

    void setParams (const HatParams& p) noexcept     { params = p; }
    const HatParams& getParams() const noexcept      { return params; }

    /** Fait sonner une frappe tout de suite (velocity de 0 à 1). */
    void trigger (bool open, float velocity) noexcept
    {
        for (auto& v : voices)
            v.choke (sampleRate);

        // Voix libre, sinon celle qui s'éteint
        HatVoice* target = nullptr;
        for (auto& v : voices)
            if (! v.isActive()) { target = &v; break; }

        if (target == nullptr)
        {
            target = &voices[0];
            for (auto& v : voices)
                if (v.isFading()) { target = &v; break; }

            target->kill();
        }

        ++hitCount;
        holdPhase = 0;   // CRUSH repart du début de la frappe
        target->start (params, open, std::clamp (velocity, 0.0f, 1.0f), sampleRate, hitSeed());
        lastHitOpen = open;
    }

    void process (float* left, float* right, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            left[i] = right[i] = 0.0f;

        // COLOR lissé par blocs de 32 échantillons
        int done = 0;
        while (done < n)
        {
            const int chunk = std::min (32, n - done);
            colorNow += (params.colorHz - colorNow) * colorSmoothing;
            widthNow += (params.width - widthNow) * colorSmoothing;

            for (auto& v : voices)
                v.render (left + done, right + done, chunk, colorNow, widthNow);

            done += chunk;
        }

        const float levelTarget = (float) std::pow (10.0, params.levelDb / 20.0) * kOutputGain;
        const float crushTarget = std::clamp (params.crush, 0.0f, 1.0f);

        for (int i = 0; i < n; ++i)
        {
            levelNow += (levelTarget - levelNow) * sampleSmoothing;
            crushNow += (crushTarget - crushNow) * sampleSmoothing;

            float l = left[i], r = right[i];

            if (crushNow > 0.002f)
            {
                // Échantillonnage divisé jusqu'à 16, résolution jusqu'à 4 bits
                const double hold = 1.0 + (double) crushNow * crushNow * 15.0 * (sampleRate / 44100.0);
                holdPhase += 1.0;
                if (holdPhase >= hold)
                {
                    holdPhase -= hold;
                    const float steps = std::pow (2.0f, 15.0f - 11.0f * crushNow);
                    heldL = std::round (l * steps) / steps;
                    heldR = std::round (r * steps) / steps;
                }

                // Fondu propre -> écrasé sur le premier pour-cent de la course
                const float wet = std::min (1.0f, crushNow * 20.0f);
                if (wet >= 1.0f)
                {
                    l = heldL;
                    r = heldR;
                }
                else
                {
                    l += (heldL - l) * wet;
                    r += (heldR - r) * wet;
                }
            }

            left[i] = softClip (l * levelNow);
            right[i] = softClip (r * levelNow);
        }
    }

    bool isActive() const noexcept
    {
        for (auto& v : voices)
            if (v.isActive())
                return true;

        return false;
    }

    uint32_t getHitCount() const noexcept      { return hitCount; }

    /** Graine de la frappe : à VARIATION 0, toujours la même (bruit et phases identiques d'un coup à l'autre) ;
        au-dessus, une nouvelle graine par frappe. */
    uint32_t hitSeed() const noexcept
    {
        return params.variation > 0.0005f ? hitCount * 0x9E3779B9u + seedBase : seedBase;
    }
    bool wasLastHitOpen() const noexcept       { return lastHitOpen; }
    void setSeedBase (uint32_t s) noexcept     { seedBase = s; }

    static float softClip (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 0.8f)
            return x;

        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0 ? -y : y;
    }

    /** Gain de sortie global (calibré : crête vers -6 dBFS à vélocité maximale). */
    static constexpr float kOutputGain = 0.5f;

private:
    void snapSmoothers() noexcept
    {
        colorNow = params.colorHz;
        widthNow = params.width;
        levelNow = (float) std::pow (10.0, params.levelDb / 20.0) * kOutputGain;
        crushNow = params.crush;
        colorSmoothing = (float) (1.0 - std::exp (-32.0 / (0.02 * sampleRate)));
        sampleSmoothing = (float) (1.0 - std::exp (-1.0 / (0.01 * sampleRate)));
    }

    double sampleRate = 48000.0;
    HatParams params;
    std::array<HatVoice, 2> voices;
    uint32_t hitCount = 0, seedBase = 0x2545F491u;
    bool lastHitOpen = false;

    float colorNow = 9500, widthNow = 0.35f, levelNow = 0.5f, crushNow = 0;
    float colorSmoothing = 0.1f, sampleSmoothing = 0.01f;
    double holdPhase = 0;
    float heldL = 0, heldR = 0;
};

} // namespace hyperhats
