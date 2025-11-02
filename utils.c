// ===================== utils.c =====================
// General utilities used across the sim.
//
// What lives here:
//   - skip_bom(FILE*):  ignore UTF-8 BOM at file start if present
//   - i_clamp/d_clamp:  clamp helpers for ints/doubles
//   - approx_binomial:  jittered binomial approximation
//   - now_ms/sleep_ms:  timing helpers (Windows)
//
// IMPORTANT: Behavior preserved exactly. This is a readability pass.

#include <stdio.h>      // FILE*, fgetc, ungetc
#include <math.h>       // sqrt, floor
#include <windows.h>    // GetTickCount64, Sleep

#include "utils.h"
#include "rng.h"        // rng01()

/* ============================== Constants & Defines ============================== */
// UTF-8 BOM bytes as named constants (instead of 0xEF,0xBB,0xBF sprinkled in code)
#define UTF8_BOM_B1                 0xEF
#define UTF8_BOM_B2                 0xBB
#define UTF8_BOM_B3                 0xBF

// Probability bounds for sanity checks
#define PROB_MIN                    0.0
#define PROB_MAX                    1.0

// Jitter settings for approx_binomial()
// We sample uniform u in [0,1), center it with (u - 0.5), and scale by sqrt(n).
// These names explain the math without changing behavior.
#define JITTER_CENTER               0.5   // centers uniform noise to [-0.5, +0.5)
#define JITTER_SQRTN_SCALE          1.0   // *exact* original behavior: 1 * sqrt(n)

// Millisecond constants are provided by Windows; no extra defines needed here.

/* ============================== File Helpers ==================================== */
/*
 * skip_bom
 * Skips a leading UTF-8 BOM (EF BB BF) if present. If not present, puts back
 * any consumed bytes so the stream is untouched.
 *
 * Why this works:
 *   - We peek the first 1–3 bytes.
 *   - If they don't match the BOM sequence, we ungetc() them in reverse order.
 *   - If they do match, we simply leave the stream positioned after the BOM.
 */
void skip_bom(FILE* f) {
    int c1 = fgetc(f);
    if (c1 != UTF8_BOM_B1) {
        if (c1 != EOF) {
            ungetc(c1, f);
        }
        return;
    }

    int c2 = fgetc(f);
    int c3 = fgetc(f);

    // If not EF BB BF, push everything back (in reverse order) so nothing is lost.
    if (!(c2 == UTF8_BOM_B2 && c3 == UTF8_BOM_B3)) {
        if (c3 != EOF) ungetc(c3, f);
        if (c2 != EOF) ungetc(c2, f);
        ungetc(c1, f);
    }
}

/* ============================== Clamp Helpers =================================== */
/*
 * i_clamp
 * Clamp integer v to the inclusive range [lo, hi].
 * Simple guardrail used throughout the sim.
 */
int i_clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/*
 * d_clamp
 * Clamp double v to the inclusive range [lo, hi].
 */
double d_clamp(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ============================== Random Helper =================================== */
/*
 * approx_binomial
 * Return an integer close to Binomial(n, p) by adding jitter around the mean.
 *
 * Idea (kept identical to original):
 *   - True mean of Binomial(n,p) is n*p.
 *   - We add a bit of noise: (rng01() - 0.5) * sqrt(n).
 *     * rng01() is uniform in [0,1), so rng01()-0.5 is in [-0.5, 0.5).
 *     * sqrt(n) widens noise with population size so it *looks* more natural.
 *   - Finally we clamp to [0, n].
 *
 * Why this exists:
 *   - It’s visually good enough for sim randomness without heavy RNG costs.
 */
int approx_binomial(int n, double p) {
    if (n <= 0 || p <= PROB_MIN) return 0;
    if (p >= PROB_MAX) return n;

    double mean = (double)n * p;
    double jitter = (rng01() - JITTER_CENTER) * (sqrt((double)n) * JITTER_SQRTN_SCALE);

    int k = (int)floor(mean + jitter);
    return i_clamp(k, 0, n);
}

/* ============================== Timing (Windows) ================================= */
/*
 * now_ms
 * Milliseconds since system start (monotonic, wraps very rarely).
 * For Windows, we use GetTickCount64() which is simple and good enough here.
 */
unsigned long long now_ms(void) {
    return (unsigned long long)GetTickCount64();
}

/*
 * sleep_ms
 * Sleep for the given number of milliseconds.
 * Windows implementation delegates to Sleep(ms).
 */
void sleep_ms(unsigned ms) {
    Sleep(ms);
}
