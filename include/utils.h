#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>  // FILE*

/*
 * Utilities shared around the simulation:
 *   - skip_bom(FILE*): ignore UTF-8 BOM if present
 *   - i_clamp/d_clamp: clamp helpers
 *   - approx_binomial: jittered binomial approximation (fast & visual)
 *   - now_ms/sleep_ms: timing helpers (Windows)
 *
 * All functions are tiny, documented, and preserve existing behavior.
 */

 // Skips a leading UTF-8 BOM (EF BB BF) if present; otherwise leaves stream untouched.
void skip_bom(FILE* f);

// Clamp integer v to [lo, hi].
int i_clamp(int v, int lo, int hi);

// Clamp double v to [lo, hi].
double d_clamp(double v, double lo, double hi);

// Fast, jittered approximation to Binomial(n, p). Returns an int in [0, n].
int approx_binomial(int n, double p);

// Milliseconds since system start (monotonic on Windows).
unsigned long long now_ms(void);

// Sleep for the given number of milliseconds.
void sleep_ms(unsigned ms);

#endif /* UTILS_H */
