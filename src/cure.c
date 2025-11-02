/* ===================== cure.c =====================
   Minimal, readable implementation with extinction-aware progress:
   - Cure progress scales with awareness and collapses as deaths rise:
       death_penalty = (1 - death_fraction)^k
   - Cure cannot activate if nobody is alive.
*/
#include <stddef.h>     /* NULL */
#include <math.h>       /* pow  */
#include "cure.h"
#include "world.h"      /* World, Cell */
#include "disease.h"    /* Disease params used on activation */

/* ---------- tiny helpers ---------- */

static double clamp_range(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Compute world totals locally (mirrors HUD logic). */
static void world_totals(const struct World* w,
    unsigned long long* out_alive,
    unsigned long long* out_infected,
    unsigned long long* out_dead)
{
    unsigned long long alive = 0ULL;
    unsigned long long inf = 0ULL;
    unsigned long long dead = 0ULL;

    if (w != NULL) {
        /* totalpop = sum of current alive across grid */
        alive = (unsigned long long)w->totalpop;

        for (int y = 0; y < w->height; y++) {
            for (int x = 0; x < w->width; x++) {
                const struct Cell* c = &w->grid[y][x];
                if (c->pop.infected > 0) inf += (unsigned long long)c->pop.infected;
                if (c->pop.dead > 0) dead += (unsigned long long)c->pop.dead;
            }
        }
    }

    if (out_alive)    *out_alive = alive;
    if (out_infected) *out_infected = inf;
    if (out_dead)     *out_dead = dead;
}

/* ---------- public API ---------- */

void cure_params_init_defaults(CureParams* cp) {
    if (cp == NULL) return;
    cp->base_awareness_per_tick = CURE_DEFAULT_BASE_AWARENESS_PER_TICK;
    cp->k_infected_awareness = CURE_DEFAULT_K_INFECTED_AWARENESS;
    cp->k_death_awareness = CURE_DEFAULT_K_DEATH_AWARENESS;
    cp->progress_per_awareness = CURE_DEFAULT_PROGRESS_PER_AWARENESS;
    cp->death_slowdown_k = CURE_DEFAULT_DEATH_SLOWDOWN_K;      /* exponent k */
    cp->post_cure_gamma_recover = CURE_DEFAULT_POST_CURE_GAMMA_RECOVER;
}

void cure_init(CureState* cs) {
    if (cs == NULL) return;
    cs->awareness = 0.0;
    cs->progress = 0.0;
    cs->cure_active = 0;
}

double cure_awareness(const CureState* cs) {
    if (cs == NULL) return 0.0;
    return cs->awareness;
}

double cure_progress(const CureState* cs) {
    if (cs == NULL) return 0.0;
    return cs->progress;
}

int cure_is_active(const CureState* cs) {
    if (cs == NULL) return 0;
    return cs->cure_active;
}

/* Update awareness and progress (pure on CureState).
   - Awareness rises from time + infections + deaths, eased near 100%.
   - Progress ∝ awareness, but multiplied by (1 - death_fraction)^k,
     so it collapses near extinction. No progress if alive==0. */
void cure_tick_update(CureState* cs, const CureParams* cp, const struct World* w) {
    if (cs == NULL || cp == NULL || w == NULL) return;
    if (cs->cure_active == 1) return;

    /* Read world state once */
    unsigned long long alive_cnt = 0ULL, inf_cnt = 0ULL, dead_cnt = 0ULL;
    world_totals(w, &alive_cnt, &inf_cnt, &dead_cnt);

    /* Fractions relative to "alive" baseline (consistent with HUD) */
    double I = 0.0;  /* infected / alive */
    double D = 0.0;  /* dead     / alive, clamped to [0,1] */

    if (alive_cnt > 0ULL) {
        I = clamp_range((double)inf_cnt / (double)alive_cnt, 0.0, 1.0);
        D = clamp_range((double)dead_cnt / (double)alive_cnt, 0.0, 1.0);
    }

    double A = cs->awareness;
    double P = cs->progress;

    /* Awareness gain this tick */
    double dA = cp->base_awareness_per_tick
        + cp->k_infected_awareness * I
        + cp->k_death_awareness * D;

    /* Ease-in near 100% awareness */
    double softness = 1.0 - (A / CURE_PERCENT_MAX);
    if (softness < 0.0) softness = 0.0;

    A += dA * softness;
    A = clamp_range(A, CURE_PERCENT_MIN, CURE_PERCENT_MAX);

    /* Progress gain with extinction-aware slowdown */
    double alive_frac = 1.0 - D;                    /* 1 when no deaths, 0 when D==1 */
    if (alive_frac < 0.0) alive_frac = 0.0;

    double k = (cp->death_slowdown_k <= 0.0) ? 1.0 : cp->death_slowdown_k;
    double death_penalty = pow(alive_frac, k);      /* collapses as deaths rise */

    double dP = cp->progress_per_awareness * (A / CURE_PERCENT_MAX) * death_penalty;

    /* If literally nobody is alive, no further progress. */
    if (alive_cnt == 0ULL) dP = 0.0;

    P += dP;
    P = clamp_range(P, CURE_PERCENT_MIN, CURE_PERCENT_MAX);

    cs->awareness = A;
    cs->progress = P;
}

/* One-time activation when progress reaches 100:
   - Only if some population is still alive.
   - Stop new infections and boost recovery. */
int  cure_maybe_activate(CureState* cs, Disease* dz, const CureParams* cp, const World* w) {
    if (cs == NULL || dz == NULL || cp == NULL || w == NULL) return 0;
    if (cs->cure_active == 1) return 0;

    /* Block activation if everyone is dead right now. */
    unsigned long long alive_cnt = 0ULL, inf_cnt = 0ULL, dead_cnt = 0ULL;
    world_totals(w, &alive_cnt, &inf_cnt, &dead_cnt);
    if (alive_cnt == 0ULL) return 0;

    if (cs->progress >= CURE_PERCENT_MAX) {
        cs->cure_active = 1;
        dz->beta_within = 0.0;
        dz->beta_neighbors = 0.0;
        dz->gamma_recover = cp->post_cure_gamma_recover;
		dz->mu_mortality = 0.0;
		dz->symptom_mult_within = 0.0;
		dz->symptom_mult_neighbor = 0.0;
		dz->mutations_enabled = 0;  /* no more mutations after cure */
        return 1;
    }
    return 0;
}
