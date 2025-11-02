// ===================== disease.h =====================
#ifndef DISEASE_H
#define DISEASE_H

#include "symptoms.h"
#include "types.h"   /* brings Cell, World, Population, Terrain */

/* --------------------------------------------------------------------------
   Public knobs
   -------------------------------------------------------------------------- */

   /* Diagonal contacts are typically less likely than orthogonal. */
#ifndef DIAG_NEIGHBOR_FACTOR
#define DIAG_NEIGHBOR_FACTOR 1.0
#endif

/* -------- Default Disease values (override at compile time if desired) ----- */
/* Example: add -DDISEASE_DEFAULT_BETA_WITHIN=0.008 to your compiler flags.   */

/* Base infection probabilities */
#ifndef DISEASE_DEFAULT_BETA_WITHIN
#define DISEASE_DEFAULT_BETA_WITHIN        0.008
#endif
#ifndef DISEASE_DEFAULT_BETA_NEIGHBORS
#define DISEASE_DEFAULT_BETA_NEIGHBORS     0.0015
#endif

/* Recovery & mortality per-tick probabilities */
#ifndef DISEASE_DEFAULT_GAMMA_RECOVER
#define DISEASE_DEFAULT_GAMMA_RECOVER      0.0
#endif
#ifndef DISEASE_DEFAULT_MU_MORTALITY
#define DISEASE_DEFAULT_MU_MORTALITY       0.0
#endif

/* Death-driven suppression exponent (higher → stronger suppression) */
#ifndef DISEASE_DEFAULT_DEATH_SUPP_K
#define DISEASE_DEFAULT_DEATH_SUPP_K       1.3
#endif

/* Ports shut down when dead% at a port reaches this threshold */
#ifndef DISEASE_DEFAULT_PORT_SHUTDOWN_PCT
#define DISEASE_DEFAULT_PORT_SHUTDOWN_PCT  50
#endif

/* Symptom aggregate multipliers (tuning knobs) */
#ifndef DISEASE_DEFAULT_SYMPTOM_MULT_WITHIN
#define DISEASE_DEFAULT_SYMPTOM_MULT_WITHIN     1.0
#endif
#ifndef DISEASE_DEFAULT_SYMPTOM_MULT_NEIGHBOR
#define DISEASE_DEFAULT_SYMPTOM_MULT_NEIGHBOR   1.0
#endif
#ifndef DISEASE_DEFAULT_SYMPTOM_MULT_MORTALITY
#define DISEASE_DEFAULT_SYMPTOM_MULT_MORTALITY  3.0
#endif

/* Mutation system toggle (0/1) at start */
#ifndef DISEASE_DEFAULT_MUTATIONS_ENABLED
#define DISEASE_DEFAULT_MUTATIONS_ENABLED       1
#endif

/* RNG seeding:
   - By default we seed with time(NULL) at runtime.
   - To use a fixed, reproducible seed, define DISEASE_DEFAULT_RNG_SEED_FIXED,
     e.g. -DDISEASE_DEFAULT_RNG_SEED_FIXED=1234
*/
#ifdef DISEASE_DEFAULT_RNG_SEED_FIXED
/* nothing else needed here; disease_init_defaults will use it */
#endif

/* --------------------------------------------------------------------------
   Disease = infection parameters + symptom state
   -------------------------------------------------------------------------- */
typedef struct Disease {
    double  beta_within;
    double  beta_neighbors;
    double  gamma_recover;
    double  mu_mortality;
    double  death_supp_k;
    int     port_shutdown_pct;
    unsigned rng_seed;

    SymptomState symptoms;

    double  symptom_mult_within;
    double  symptom_mult_neighbor;
    double  symptom_mult_mortality;

    int     mutations_enabled;   /* 0/1 */
} Disease;

/* Mortality / ports */
void apply_mortality_world(World* w, const Disease* dz, int use_recovery);
void update_all_ports(World* w, const Disease* dz);

/* Infectivity suppression (death-driven) */
double effective_beta_within(const Cell* c, const Disease* dz);
double effective_beta_neighbors(const Cell* c, const Disease* dz);

/* Ship helpers */
int can_spawn_from_port(const Cell* c, const Disease* dz);
int can_infect_on_arrival(const Cell* dest, const Disease* dz);

/* Infection updates used by world.c::step_world */
void update_cell_infections(World* w, const Disease* dz);
void spread_to_neighbors(World* w, const Disease* dz);

/* Ratios (handy for render and HUD math) */
double cell_initial_total(const Cell* c);
double cell_dead_fraction(const Cell* c);
double cell_alive_fraction(const Cell* c);

/* Defaults and symptom refresh */
void disease_init_defaults(Disease* dz);
/* Recompute effective params from current symptoms (no compounding). */
void disease_refresh_effective_params_from_symptoms(struct Disease* dz);

#endif /* DISEASE_H */
