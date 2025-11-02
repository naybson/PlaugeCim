/* ===================== cure.h =====================
   Humanity Awareness → Cure system
   - All tunables have compile-time defaults (override with -D…)
   - Clear, simple API; no heavy includes to avoid cycles
*/
#ifndef CURE_H
#define CURE_H
#include <stdio.h>
#include <stdlib.h>
#include "disease.h"

/* Forward declarations to avoid heavy includes/cycles */
struct World;
struct Disease;

/* ------------------------------------------------------------
 * Public knobs (defaults; override via compiler -D flags)
 * ------------------------------------------------------------ */
#ifndef CURE_PERCENT_MIN
#define CURE_PERCENT_MIN                 0.0    /* lower clamp for awareness/progress */
#endif
#ifndef CURE_PERCENT_MAX
#define CURE_PERCENT_MAX               100.0    /* upper clamp for awareness/progress */
#endif

 /* Awareness growth terms per tick */
#ifndef CURE_DEFAULT_BASE_AWARENESS_PER_TICK
#define CURE_DEFAULT_BASE_AWARENESS_PER_TICK    0.005   /* gentle drift even with no cases */
#endif
#ifndef CURE_DEFAULT_K_INFECTED_AWARENESS
#define CURE_DEFAULT_K_INFECTED_AWARENESS       0.75    /* weight for infected fraction [0..1] */
#endif
#ifndef CURE_DEFAULT_K_DEATH_AWARENESS
#define CURE_DEFAULT_K_DEATH_AWARENESS          1.25    /* weight for death fraction [0..1] */
#endif

/* Research progress speed (per unit of awareness) */
#ifndef CURE_DEFAULT_PROGRESS_PER_AWARENESS
#define CURE_DEFAULT_PROGRESS_PER_AWARENESS     0.60
#endif

/* Production slowdown as deaths rise (larger → stronger slowdown) */
#ifndef CURE_DEFAULT_DEATH_SLOWDOWN_K
#define CURE_DEFAULT_DEATH_SLOWDOWN_K           2.0
#endif

/* Post-cure recovery boost (gamma after activation) */
#ifndef CURE_DEFAULT_POST_CURE_GAMMA_RECOVER
#define CURE_DEFAULT_POST_CURE_GAMMA_RECOVER    0.70
#endif

/* ------------------------------------------------------------
 * Tunables container (typically lives inside SimConfig)
 * ------------------------------------------------------------ */
typedef struct {
    double base_awareness_per_tick;   /* slow drift over time */
    double k_infected_awareness;      /* weight for infected fraction [0..1] */
    double k_death_awareness;         /* weight for death fraction    [0..1] */
    double progress_per_awareness;    /* research efficiency per awareness */
    double death_slowdown_k;          /* deaths reduce production speed */
    double post_cure_gamma_recover;   /* e.g., 0.70 once cure is active */
} CureParams;

/* Runtime state (belongs in your GameState) */
typedef struct {
    double awareness;    /* [0..100] */
    double progress;     /* [0..100] */
    int    cure_active;  /* 0 until progress reaches 100 once */
} CureState;

/* ------------------------------------------------------------
 * Lifecycle / utilities
 * ------------------------------------------------------------ */

 /* Fill a CureParams with compile-time defaults (nice single source of truth). */
void cure_params_init_defaults(CureParams* cp);

/* Reset runtime cure state. Does not touch Disease. */
void cure_init(CureState* cs);

/* Per-tick update using world totals (pure; no side-effects on Disease). */
void cure_tick_update(CureState* cs, const CureParams* cp, const struct World* w);

/* If progress hit 100 this tick, apply cure effects to Disease and return 1. */
int cure_maybe_activate(CureState*, Disease*, const CureParams*, const World*);

/* Accessors (handy for HUD/tests) */
double cure_awareness(const CureState* cs);
double cure_progress(const CureState* cs);
int    cure_is_active(const CureState* cs);

#endif /* CURE_H */
