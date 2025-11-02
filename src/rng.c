// ===================== rng.c =====================
// Purpose: Tiny, deterministic RNG based on xorshift32.
// Exposes three functions:
//   - rng_seed(s): set the internal seed (0 becomes 1 to avoid a stuck state)
//   - rng_u32():   next 32-bit pseudo-random value
//   - rng01():     double in ~[0,1]; see note below
//
// Implementation notes (kept identical to original behavior):
//   • Core algorithm is xorshift32 (three XOR+shift steps).
//   • We keep the exact shift constants (13, 17, 5) as named defines.
//   • rng01() divides by UINT_MAX, so 1.0 is *technically possible* when the
//     raw 32-bit value equals UINT_MAX. This matches your original logic.

#include "rng.h"   // public API + <limits.h>

// ===================== Constants & Defines =====================
// Fallback seed when 0 is provided (xorshift32 gets stuck at 0).
#define RNG_DEFAULT_SEED  1u

// xorshift32 shift constants (classic parameters)
#define XORSHIFT_A        13
#define XORSHIFT_B        17
#define XORSHIFT_C        5

// ===================== Module State =====================
// Internal RNG state. Nonzero required; 1 is a conventional start value.
static unsigned rng_state = RNG_DEFAULT_SEED;

// ===================== Public API =====================

/*
 * rng_seed
 * Set internal RNG state.
 * If s == 0, we use RNG_DEFAULT_SEED to avoid the xorshift32 zero trap.
 */
void rng_seed(unsigned s) {
    if (s == 0u) {
        rng_state = RNG_DEFAULT_SEED;
    }
    else {
        rng_state = s;
    }
}

/*
 * rng_u32
 * Generate next 32-bit pseudo-random value using xorshift32.
 * Explanation (simple): we scramble the bits of the current state
 * by shifting and XORing them a few times; that new value becomes
 * both the output and the next state.
 */
unsigned rng_u32(void) {
    unsigned x = rng_state;

    // The three xorshift steps (order and constants matter)
    x ^= x << XORSHIFT_A;
    x ^= x >> XORSHIFT_B;
    x ^= x << XORSHIFT_C;

    rng_state = x;
    return x;
}

/*
 * rng01
 * Return a double in [0, 1] *per the original implementation* by dividing
 * the 32-bit integer by UINT_MAX. That means 1.0 is rare but possible.
 * (Many RNGs use 2^32 as the divisor to ensure [0,1), but we keep your logic.)
 */
double rng01(void) {
    return rng_u32() / (double)UINT_MAX;
}
