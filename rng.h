#ifndef RNG_H
#define RNG_H

#include <limits.h>  // UINT_MAX

/*
 * RNG module (xorshift32).
 * Deterministic, fast, and portable.
 *
 * Functions:
 *   void     rng_seed(unsigned s);  // set seed (0 becomes 1 internally)
 *   unsigned rng_u32(void);         // next 32-bit value
 *   double   rng01(void);           // ~[0,1]; note: 1.0 is possible with UINT_MAX
 */

 // Initialize RNG with a seed (0 will be replaced with a safe nonzero seed).
void rng_seed(unsigned s);

// Get next random 32-bit unsigned int.
unsigned rng_u32(void);

// Get a random double in ~[0,1]; 1.0 is possible when value == UINT_MAX.
double rng01(void);

#endif /* RNG_H */
