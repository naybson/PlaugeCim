#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include "game_defs.h"
#include "cure.h"

/* Runtime knobs edited in the setup menu */
typedef struct {
    CureParams cure;            /* awareness → cure parameters */
    double     mutation_chance_pct; /* % chance per tick for a mutation attempt */
} SimConfig;

/* Named defaults (easy to tweak/override via -D…) */
#ifndef DEFAULT_BASE_AWARE_TICK
#define DEFAULT_BASE_AWARE_TICK       0.010
#endif
#ifndef DEFAULT_K_INF_AWARE
#define DEFAULT_K_INF_AWARE           0.30
#endif
#ifndef DEFAULT_K_DEATH_AWARE
#define DEFAULT_K_DEATH_AWARE         2.00
#endif
#ifndef DEFAULT_PROGRESS_PER_AWARE
#define DEFAULT_PROGRESS_PER_AWARE    0.20
#endif
#ifndef DEFAULT_DEATH_SLOWDOWN_K
#define DEFAULT_DEATH_SLOWDOWN_K      1.000
#endif
#ifndef DEFAULT_POST_CURE_GAMMA
#define DEFAULT_POST_CURE_GAMMA       0.700
#endif
#ifndef DEFAULT_MUTATION_CHANCE_PCT
#define DEFAULT_MUTATION_CHANCE_PCT   0.25
#endif


/* One-stop initializer for menu defaults */
static inline void simconfig_init_defaults(SimConfig* cfg)
{
    if (cfg == NULL) return;
    cfg->cure.base_awareness_per_tick = DEFAULT_BASE_AWARE_TICK;
    cfg->cure.k_infected_awareness = DEFAULT_K_INF_AWARE;
    cfg->cure.k_death_awareness = DEFAULT_K_DEATH_AWARE;
    cfg->cure.progress_per_awareness = DEFAULT_PROGRESS_PER_AWARE;
    cfg->cure.death_slowdown_k = DEFAULT_DEATH_SLOWDOWN_K;
    cfg->cure.post_cure_gamma_recover = DEFAULT_POST_CURE_GAMMA;
    cfg->mutation_chance_pct = DEFAULT_MUTATION_CHANCE_PCT;
}

#endif /* CONFIG_H */
