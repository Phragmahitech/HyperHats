// Tests du moteur HyperHats (sans JUCE).

#include "HatEngine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hyperhats;

static int failures = 0, checks = 0;

#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  ECHEC (%s:%d) ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct Render
{
    std::vector<float> left, right;
};

/** Rend `ms` millisecondes ; les frappes sont (temps en ms, open, vélocité). */
struct Hit { double atMs; bool open; float velocity; };

static Render render (const HatParams& p, const std::vector<Hit>& hits, double ms, double sr = 48000.0, HatEngine* engineOut = nullptr)
{
    HatEngine local;
    HatEngine& e = engineOut != nullptr ? *engineOut : local;
    e.setParams (p);
    e.prepare (sr);

    const int total = (int) (ms * 0.001 * sr);
    Render r;
    r.left.assign ((size_t) total, 0.0f);
    r.right.assign ((size_t) total, 0.0f);

    int pos = 0;
    size_t next = 0;

    while (pos < total)
    {
        int end = total;
        if (next < hits.size())
            end = std::min (total, (int) (hits[next].atMs * 0.001 * sr));

        if (end > pos)
        {
            e.process (r.left.data() + pos, r.right.data() + pos, end - pos);
            pos = end;
        }

        if (next < hits.size() && pos >= (int) (hits[next].atMs * 0.001 * sr))
        {
            e.trigger (hits[next].open, hits[next].velocity);
            ++next;
        }
    }

    return r;
}

static float peakOf (const std::vector<float>& x, size_t from = 0, size_t to = (size_t) -1)
{
    float pk = 0;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        pk = std::max (pk, std::abs (x[i]));
    return pk;
}

static double rmsOf (const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min (to, x.size());
    if (to <= from) return 0.0;
    double s = 0;
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (to - from));
}

static bool allFinite (const Render& r)
{
    for (size_t i = 0; i < r.left.size(); ++i)
        if (! std::isfinite (r.left[i]) || ! std::isfinite (r.right[i]))
            return false;
    return true;
}

/** Temps (ms) après lequel l'enveloppe (RMS sur 2 ms) reste sous `db` sous la crête. */
static double timeBelow (const std::vector<float>& x, double db, double sr)
{
    const size_t win = (size_t) (0.002 * sr);
    double peakRms = 0;
    std::vector<double> env;
    for (size_t i = 0; i + win <= x.size(); i += win)
    {
        env.push_back (rmsOf (x, i, i + win));
        peakRms = std::max (peakRms, env.back());
    }

    const double threshold = peakRms * std::pow (10.0, db / 20.0);
    size_t last = 0;
    for (size_t i = 0; i < env.size(); ++i)
        if (env[i] > threshold)
            last = i;

    return (double) (last + 1) * win * 1000.0 / sr;
}

static double zeroCrossRate (const std::vector<float>& x, size_t from, size_t to)
{
    int n = 0;
    for (size_t i = from + 1; i < std::min (to, x.size()); ++i)
        if ((x[i - 1] < 0) != (x[i] < 0)) ++n;
    return (double) n / (double) (to - from);
}

static HatParams plain()
{
    HatParams p;
    p.variation = 0.0f;
    p.width = 0.0f;
    p.crush = 0.0f;
    return p;
}

int main()
{
    const double sr = 48000.0;

    std::printf ("Silence avant toute frappe\n");
    {
        auto r = render (plain(), {}, 200);
        CHECK (peakOf (r.left) == 0.0f && peakOf (r.right) == 0.0f, "sortie non nulle sans frappe");
    }

    std::printf ("Tous les alliages, sources et characters : son audible, fini, borné, voix qui s'éteint\n");
    for (int alloy = 0; alloy < 3; ++alloy)
        for (float c : { 0.0f, 0.5f, 1.0f })
            for (float s : { 0.0f, 0.5f, 1.0f })
                for (bool open : { false, true })
                {
                    HatParams p = plain();
                    p.alloy = alloy; p.character = c; p.source = s;
                    HatEngine e;
                    auto r = render (p, { { 0.0, open, 1.0f } }, open ? 4000.0 : 1200.0, sr, &e);
                    const float pk = peakOf (r.left);
                    CHECK (allFinite (r), "NaN/inf alliage %d c=%.1f s=%.1f", alloy, c, s);
                    CHECK (pk <= 1.0f, "dépasse 0 dBFS (%.3f)", pk);
                    CHECK (pk > 0.12f, "trop faible : alliage %d c=%.1f s=%.1f open=%d crête %.3f", alloy, c, s, (int) open, pk);
                    CHECK (! e.isActive(), "voix encore active à la fin (alliage %d, open=%d)", alloy, (int) open);
                }

    std::printf ("Équilibre métal / bruit (après filtre) : moins de 6 dB d'écart\n");
    for (int alloy = 0; alloy < 3; ++alloy)
        for (float color : { 3000.0f, 9500.0f, 14000.0f })
        {
            HatParams p = plain();
            p.alloy = alloy; p.colorHz = color; p.snap = 0; p.shape = 1; p.openMs = 2000; p.character = 0;
            p.source = 0; auto metal = render (p, { { 0, true, 1 } }, 200);
            p.source = 1; auto noise = render (p, { { 0, true, 1 } }, 200);
            const double ratio = 20.0 * std::log10 (rmsOf (metal.left, 0, metal.left.size()) / rmsOf (noise.left, 0, noise.left.size()));
            CHECK (std::abs (ratio) < 6.0, "alliage %d à %.0f Hz : métal %.1f dB par rapport au bruit", alloy, color, ratio);
        }

    std::printf ("Durées : closed plus court que open, longueur proche du réglage\n");
    for (float D : { 30.0f, 120.0f, 600.0f, 2000.0f })
    {
        HatParams p = plain();
        p.shape = 0; p.snap = 0; p.source = 1; p.character = 0;
        p.openMs = D;
        auto r = render (p, { { 0, true, 1 } }, D * 3.5 + 50);
        const double t35 = timeBelow (r.left, -35.0, sr);
        CHECK (t35 > D * 0.7 && t35 < D * 1.35, "decay %.0f ms : -35 dB atteint à %.1f ms", D, t35);
    }
    {
        HatParams p = plain();
        auto c = render (p, { { 0, false, 1 } }, 1500);
        auto o = render (p, { { 0, true, 1 } }, 1500);
        CHECK (timeBelow (c.left, -40, sr) * 4 < timeBelow (o.left, -40, sr), "closed pas assez court par rapport à open");
    }

    std::printf ("SHAPE gaté : plus rien après la durée\n");
    {
        HatParams p = plain();
        p.shape = 1; p.openMs = 200; p.snap = 0; p.source = 1;
        auto r = render (p, { { 0, true, 1 } }, 400);
        const double after = rmsOf (r.left, (size_t) (0.205 * sr), (size_t) (0.4 * sr));
        CHECK (after < 1e-6, "son après la fin du gate (%.2e)", after);
        const double during = rmsOf (r.left, (size_t) (0.10 * sr), (size_t) (0.12 * sr));
        const double start = rmsOf (r.left, (size_t) (0.005 * sr), (size_t) (0.025 * sr));
        CHECK (during > start * 0.5, "le gate ne tient pas (%.3f contre %.3f)", during, start);
    }

    std::printf ("Choke : le closed coupe l'open\n");
    {
        HatParams p = plain();
        p.openMs = 2000; p.closedMs = 20; p.shape = 0;
        auto alone = render (p, { { 0, true, 1 } }, 600);
        auto choked = render (p, { { 0, true, 1 }, { 100, false, 1 } }, 600);
        const size_t from = (size_t) (0.25 * sr), to = (size_t) (0.6 * sr);
        CHECK (rmsOf (alone.left, from, to) > 0.01, "l'open seul devrait encore sonner");
        CHECK (rmsOf (choked.left, from, to) < 1e-5, "l'open n'est pas coupé (%.2e)", rmsOf (choked.left, from, to));

        // Pas de saut brutal au moment de la coupure : le fondu dure 3 ms
        const size_t at = (size_t) (0.1 * sr);
        double maxJumpBefore = 0;
        for (size_t i = at - 2000; i < at; ++i) maxJumpBefore = std::max (maxJumpBefore, (double) std::abs (choked.left[i] - choked.left[i - 1]));
        CHECK (maxJumpBefore < 2.0, "valeurs incohérentes avant la coupure");
    }

    std::printf ("Largeur : mono strict à 0, décorrélé à 100 %%\n");
    {
        HatParams p = plain();
        p.variation = 0.6f; p.width = 0.0f; p.source = 0.7f;
        auto mono = render (p, { { 0, true, 1 }, { 300, false, 1 }, { 400, true, 0.7f } }, 900);
        bool same = true;
        for (size_t i = 0; i < mono.left.size(); ++i) if (mono.left[i] != mono.right[i]) { same = false; break; }
        CHECK (same, "gauche et droite différents avec WIDTH à 0");

        p.width = 1.0f; p.source = 1.0f;
        auto wide = render (p, { { 0, true, 1 } }, 400);
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = 0; i < wide.left.size(); ++i) { lr += wide.left[i] * wide.right[i]; ll += wide.left[i] * wide.left[i]; rr += wide.right[i] * wide.right[i]; }
        const double corr = lr / std::sqrt (ll * rr);
        CHECK (std::abs (corr) < 0.3, "corrélation gauche / droite %.2f à WIDTH 100 %%", corr);
        const double ratio = std::sqrt (ll / rr);
        CHECK (ratio > 0.7 && ratio < 1.4, "déséquilibre gauche / droite %.2f", ratio);
    }

    std::printf ("COLOR et TUNE déplacent le timbre\n");
    {
        HatParams p = plain();
        p.source = 1; p.colorHz = 3000;
        auto low = render (p, { { 0, true, 1 } }, 300);
        p.colorHz = 14000;
        auto high = render (p, { { 0, true, 1 } }, 300);
        CHECK (zeroCrossRate (high.left, 0, high.left.size()) > zeroCrossRate (low.left, 0, low.left.size()) * 1.5, "COLOR n'éclaircit pas");

        for (int alloy = 0; alloy < 3; ++alloy)
        {
            HatParams q = plain();
            q.source = 0; q.alloy = alloy; q.colorHz = 5000; q.snap = 0; q.character = 0;
            q.tune = -12; auto down = render (q, { { 0, true, 1 } }, 300);
            q.tune = 12;  auto up = render (q, { { 0, true, 1 } }, 300);
            CHECK (zeroCrossRate (up.left, 0, up.left.size()) > zeroCrossRate (down.left, 0, down.left.size()) * 1.15, "TUNE sans effet (alliage %d)", alloy);
        }
    }

    std::printf ("Vélocité, LEVEL, SNAP\n");
    {
        HatParams p = plain();
        auto loud = render (p, { { 0, true, 1.0f } }, 300);
        auto soft = render (p, { { 0, true, 0.2f } }, 300);
        CHECK (rmsOf (soft.left, 0, soft.left.size()) < rmsOf (loud.left, 0, loud.left.size()) * 0.4, "vélocité faible pas assez douce");

        p.levelDb = -12;
        auto quiet = render (p, { { 0, true, 1.0f } }, 300);
        const double r = rmsOf (quiet.left, 0, quiet.left.size()) / rmsOf (loud.left, 0, loud.left.size());
        CHECK (r > 0.2 && r < 0.3, "LEVEL -12 dB donne un rapport %.3f", r);

        HatParams s = plain();
        s.snap = 0; auto noSnap = render (s, { { 0, false, 1 } }, 20);
        s.snap = 1; auto withSnap = render (s, { { 0, false, 1 } }, 20);
        const size_t ms1 = (size_t) (0.001 * sr);
        CHECK (rmsOf (withSnap.left, 0, ms1) > rmsOf (noSnap.left, 0, ms1) * 1.3, "SNAP sans effet sur l'attaque");
    }

    std::printf ("CRUSH\n");
    {
        HatParams p = plain();
        p.crush = 1.0f;
        auto r = render (p, { { 0, true, 1 } }, 300);
        int repeats = 0;
        for (size_t i = 1; i < 4800; ++i) if (r.left[i] == r.left[i - 1]) ++repeats;
        CHECK (repeats > 4000, "CRUSH ne réduit pas l'échantillonnage (%d répétitions)", repeats);
        CHECK (peakOf (r.left) <= 1.0f && allFinite (r), "CRUSH hors bornes");
    }

    std::printf ("Même suite de frappes = même son (reproductible)\n");
    {
        HatParams p; // variation et largeur par défaut
        std::vector<Hit> hits;
        for (int i = 0; i < 16; ++i) hits.push_back ({ i * 78.9, i % 4 == 2, 0.6f + 0.025f * i });
        auto a = render (p, hits, 1500);
        auto b = render (p, hits, 1500);
        CHECK (a.left == b.left && a.right == b.right, "deux rendus identiques diffèrent");

        // VARIATION : deux frappes identiques sonnent différemment
        HatParams v = plain();
        v.variation = 1.0f;
        auto twice = render (v, { { 0, false, 1 }, { 100, false, 1 } }, 200);
        const double r1 = rmsOf (twice.left, 0, 2400), r2 = rmsOf (twice.left, 4800, 7200);
        CHECK (std::abs (r1 - r2) > 1e-4, "VARIATION sans effet");
    }


    std::printf ("VARIATION 0 : toutes les frappes identiques, au bit près\n");
    for (int alloy = 0; alloy < 3; ++alloy)
        for (bool open : { false, true })
        {
            HatParams p;
            p.alloy = alloy; p.variation = 0.0f; p.character = 0.8f; p.source = 0.6f; p.width = 0.6f; p.crush = 0.3f;
            auto r = render (p, { { 0, open, 0.8f }, { 3000, open, 0.8f }, { 6000, open, 0.8f } }, 9000);
            const size_t len = (size_t) (2.5 * sr);
            double maxDiff = 0;
            for (int k = 1; k < 3; ++k)
                for (size_t i = 0; i < len; ++i)
                {
                    const size_t j = (size_t) (k * 3.0 * sr) + i;
                    maxDiff = std::max (maxDiff, (double) std::abs (r.left[j] - r.left[i]));
                    maxDiff = std::max (maxDiff, (double) std::abs (r.right[j] - r.right[i]));
                }
            CHECK (maxDiff == 0.0, "alliage %d open=%d : frappes différentes à VARIATION 0 (écart %.2e)", alloy, (int) open, maxDiff);
        }

    std::printf ("Fréquences d'échantillonnage et réglages au hasard\n");
    {
        Noise rng;
        rng.seed (2024);
        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            for (int round = 0; round < 12; ++round)
            {
                HatParams p;
                p.alloy = (int) (rng.unit() * 3) % 3;
                p.source = rng.unit(); p.character = rng.unit(); p.tune = -12 + 24 * rng.unit();
                p.colorHz = 2000 * std::pow (8.0f, rng.unit()); p.snap = rng.unit();
                p.closedMs = 10 * std::pow (30.0f, rng.unit()); p.openMs = 80 * std::pow (31.25f, rng.unit());
                p.shape = rng.unit(); p.width = rng.unit(); p.variation = rng.unit(); p.crush = rng.unit();
                p.levelDb = -24 + 30 * rng.unit();

                std::vector<Hit> hits;
                double t = 0;
                while (t < 1500) { hits.push_back ({ t, rng.unit() > 0.6f, rng.unit() }); t += 0.5 + 120 * rng.unit() * rng.unit(); }
                auto r = render (p, hits, 1800, rate);
                CHECK (allFinite (r), "NaN à %.0f Hz", rate);
                CHECK (peakOf (r.left) <= 1.0f && peakOf (r.right) <= 1.0f, "dépassement à %.0f Hz", rate);
            }
    }

    std::printf ("Charge processeur (pire cas : acoustique, stéréo, frappes en double-croches à 200 BPM)\n");
    {
        HatParams p = plain();
        p.character = 1; p.width = 1; p.variation = 1; p.crush = 0.5f; p.openMs = 2500; p.shape = 0;
        std::vector<Hit> hits;
        for (int i = 0; i < 400; ++i) hits.push_back ({ i * 75.0, i % 2 == 0, 1.0f });
        const auto t0 = std::chrono::steady_clock::now();
        auto r = render (p, hits, 30000);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  30 s d'audio en %.3f s (%.2f %% d'un cœur)\n", seconds, seconds / 30.0 * 100.0);
        CHECK (seconds < 3.0, "trop lent");
        CHECK (allFinite (r), "NaN");
    }

    std::printf ("\n%d vérifications, %d échec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
