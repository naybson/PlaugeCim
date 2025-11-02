// ===================== disease.c =====================
#include <stdlib.h>    /* calloc, free */
#include <math.h>      /* pow */
#include <stdio.h>     /* fprintf (errors) */
#include <time.h>      /* time() for default RNG seed */

#include "types.h"
#include "world.h"
#include "rng.h"
#include "disease.h"
#include "symptoms.h"
#include "config.h"    /* URBAN/RURAL multipliers, PROB_MIN/PROB_MAX, etc. */

/* --------------------------------------------------------------------------
   Safety clamps and fallbacks (kept here for self-containment)
   -------------------------------------------------------------------------- */

   /* Probability clamps (keep numeric stability) */
#ifndef PROB_MIN
#define PROB_MIN  (0.0)
#endif
#ifndef PROB_MAX
#define PROB_MAX  (1.0)
#endif

/* Neutral climate marker for symptom refresh (avoids hot/cold bonus) */
static const Climate NEUTRAL_CLIMATE_FOR_REFRESH = (Climate)(-1);

/* --------------------------------------------------------------------------
   Small, readable helpers
   -------------------------------------------------------------------------- */
static double clamp_prob(double p) {
    if (p < PROB_MIN) return PROB_MIN;
    if (p > PROB_MAX) return PROB_MAX;
    return p;
}

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static double clamp01(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

static double safe_div(double num, double den) {
    if (den <= 0.0) return 0.0;
    return num / den;
}

/* Repeated Bernoulli trials: simple, predictable, and good enough for clarity. */
static int sample_bern_count(int trials, double p) {
    if (trials <= 0) return 0;
    if (p <= 0.0) return 0;
    if (p >= 1.0) return trials;

    int count = 0;
    for (int i = 0; i < trials; ++i) {
        double r = rng01();
        if (r < p) {
            count += 1;
        }
    }
    return count;
}

/* --------------------------------------------------------------------------
   Land-use multipliers
   -------------------------------------------------------------------------- */
static void landuse_multipliers(const Cell* src, double* k_within, double* k_neighbor) {
    if (k_within == NULL || k_neighbor == NULL) return;

    if (src->settlement == SETTLE_URBAN) {
        *k_within = URBAN_WITHIN_K;
        *k_neighbor = URBAN_NEIGHBOR_K;
    }
    else {
        *k_within = RURAL_WITHIN_K;
        *k_neighbor = RURAL_NEIGHBOR_K;
    }
}

/* --------------------------------------------------------------------------
   Ratios derived from current population
   -------------------------------------------------------------------------- */
double cell_initial_total(const Cell* c) {
    if (c == NULL) return 0.0;
    return (double)c->pop.total + (double)c->pop.dead;
}

double cell_dead_fraction(const Cell* c) {
    double init = cell_initial_total(c);
    if (init <= 0.0) return 0.0;
    return clamp01((double)c->pop.dead / init);
}

double cell_alive_fraction(const Cell* c) {
    double init = cell_initial_total(c);
    if (init <= 0.0) return 0.0;
    return clamp01((double)c->pop.total / init);
}

/* --------------------------------------------------------------------------
   Mortality & Recovery step (per-cell and whole world)
   -------------------------------------------------------------------------- */
static void apply_mortality_to_cell(Cell* c, const Disease* dz, int use_recovery)
{
    if (c == NULL || dz == NULL) return;

    /* Only Land or Port cells can hold people (sea cells are skipped). */
    {
        int is_land_or_port = 0;
        if (c->terrain == TERRAIN_LAND) is_land_or_port = 1;
        if (c->raw == 'P')              is_land_or_port = 1;
        if (is_land_or_port == 0)       return;
    }

    /* If nobody is alive or nobody is infected, nothing to do. */
    if (c->pop.total <= 0) return;
    if (c->pop.infected <= 0) return;

    /* Snapshot infected at the START of this mortality step.
       We use this fixed number so deaths and recoveries are computed from the same baseline. */
    const int n_inf = c->pop.infected;

    /* ---------------- 1) Deaths among infected ----------------
       - Per infected person, death happens this tick with probability dz->mu_mortality (μ).
       - We sample a count, then clamp it into [0, n_inf] for safety. */
    int died_now = 0;
    if (dz->mu_mortality > 0.0) {
		// out of all the infected people they have mu mortality chance to  die, how many we get?
        died_now = sample_bern_count(n_inf, dz->mu_mortality);
        died_now = clamp_int(died_now, 0, n_inf);
    }

    /* ---------------- 2) Recoveries among remaining infected (optional) ----------------
       - If recovery is enabled, survivors from step 1 can recover with probability γ.
       - still_inf = n_inf - died_now (the ones who did not die this tick).
       - We sample recoveries from still_inf, then clamp to [0, still_inf]. */
    int recovered_now = 0;
    if (use_recovery != 0 && dz->gamma_recover > 0.0) {
        int still_inf = n_inf - died_now;
        if (still_inf > 0) {
            // out of all the infctead peaple they have gamma recover change to get better, how many we get?
            recovered_now = sample_bern_count(still_inf, dz->gamma_recover); 
            recovered_now = clamp_int(recovered_now, 0, still_inf);
        }
    }

    /* ---------------- 3) Apply deltas to the cell ----------------
       - Infected decreases by (deaths + recoveries).
       - Total living decreases by deaths only.
       - Dead increases by deaths.
       - After each write, clamp to avoid negative counts. */
    c->pop.infected -= (died_now + recovered_now);
    if (c->pop.infected < 0) c->pop.infected = 0;

    c->pop.total -= died_now;
    if (c->pop.total < 0) c->pop.total = 0;

    c->pop.dead += died_now;
    if (c->pop.dead < 0) c->pop.dead = 0;
}

void apply_mortality_world(World* w, const Disease* dz, int use_recovery) {
    if (w == NULL || dz == NULL) return;

    for (int r = 0; r < w->height; ++r) {
        for (int c = 0; c < w->width; ++c) {
            apply_mortality_to_cell(&w->grid[r][c], dz, use_recovery);
        }
    }
}

/* --------------------------------------------------------------------------
   Ports: active/inactive based on death %
   -------------------------------------------------------------------------- */
static int port_is_inactive_by_threshold(const Cell* c, const Disease* dz) {
    if (c == NULL || dz == NULL) return 1;

    /* Ports are identified by raw char 'P' throughout the codebase. */
    if (c->raw != 'P') {
        return 0; /* Not a port → cannot be an "inactive port". */
    }

    double init = cell_initial_total(c);
    if (init <= 0.0) return 1; /* nobody ever lived here */

    double dead_pct = cell_dead_fraction(c) * 100.0;
    if (dead_pct >= (double)dz->port_shutdown_pct) {
        return 1;
    }
    return 0;
}

void update_all_ports(World* w, const Disease* dz) {
    if (w == NULL || dz == NULL) return;

    /* Visit every cell and only act on raw 'P' (ports) */
    for (int r = 0; r < w->height; ++r) {
        for (int c = 0; c < w->width; ++c) {
            Cell* cell = &w->grid[r][c];

            /* Only ports have an open/closed status */
            if (cell->raw != 'P') {
                continue;
            }

            /* Dead percentage at this port (0..100). Uses initial baseline. */
            double dead_pct = cell_dead_fraction(cell) * 100.0;

            /* Threshold rule: at or above → CLOSE, else OPEN. */
            if ((int)dead_pct >= dz->port_shutdown_pct) {
                cell->port_active = 0;   /* closed: cannot receive infection */
            }
            else {
                cell->port_active = 1;   /* open: can receive infection */
            }
        }
    }
}

/* --------------------------------------------------------------------------
   Death-driven suppression of infectivity
   -------------------------------------------------------------------------- */
static double suppression_from_death(const Cell* c, double k) 
{
    double df = cell_dead_fraction(c);
    double one_minus = 1.0 - df;
    if (one_minus < 0.0) one_minus = 0.0;
    if (k <= 0.0) return 1.0;          /* no suppression if silly k */
    return pow(one_minus, k);
}

double effective_beta_within(const Cell* c, const Disease* dz) {
    if (c == NULL || dz == NULL) return 0.0;
    double s = suppression_from_death(c, dz->death_supp_k);
    double b = dz->beta_within * s;
    if (b < 0.0) b = 0.0;
    return b;
}

double effective_beta_neighbors(const Cell* c, const Disease* dz) 
{
    if (c == NULL || dz == NULL) return 0.0;
    double s = suppression_from_death(c, dz->death_supp_k);
    double b = dz->beta_neighbors * s;
    if (b < 0.0) b = 0.0;
    return b;
}

/* --------------------------------------------------------------------------
   Within-cell infections
   -------------------------------------------------------------------------- */
   /* --------------------------------------------------------------------------
      Within-cell infections (one tick)
      What this does:
      - Skip sea cells (only Land/Port can hold people).
      - For each eligible cell, compute p = clamp01(beta_eff_within * (I / alive)).
      - Draw Binomial(S, p) new infections (S = susceptibles = alive - infected).
      - Apply the delta with safety clamps (no negatives / no overflow).
   ----------------------------------------------------------------------------*/
void update_cell_infections(World* w, const Disease* dz) {
    if (w == NULL || dz == NULL) return;

    for (int r = 0; r < w->height; ++r) {
        for (int c = 0; c < w->width; ++c) {
            Cell* cell = &w->grid[r][c];

            /* Gate: only land or port cells can host population */
            int is_land_or_port = 0;
            if (cell->terrain == TERRAIN_LAND) is_land_or_port = 1;
            if (cell->raw == 'P')              is_land_or_port = 1;
            if (is_land_or_port == 0)          continue;

            /* Current counts */
            int alive = cell->pop.total;      /* living people in cell */
            int infected = cell->pop.infected;   /* currently infected    */
            if (alive <= 0) continue;

            /* Susceptibles = alive - infected (protected against negatives) */
            int susceptible = alive - infected;
            if (susceptible < 0) susceptible = 0;

            /* If nobody infectious or nobody susceptible → no within-cell spread */
            if (infected <= 0 || susceptible <= 0) continue;

            /* Prevalence (infection "pressure") = I / alive (safe division) */
            double pressure = safe_div((double)infected, (double)alive); 

            /* Land-use multiplier: urban vs rural (within-cell factor kW) */
            double kW = 1.0, kN_unused = 1.0;
            landuse_multipliers(cell, &kW, &kN_unused);

            /* Effective within-cell transmission rate (includes death suppression) */
            double b_eff = effective_beta_within(cell, dz) * kW; 
            b_eff = clamp_prob(b_eff);          /* keep numeric stability */

            /* Per-tick infection probability p in [0,1] */
            double p_infect = b_eff * pressure; /* linear model: β_eff * I/alive */
            p_infect = clamp01(p_infect);

            /* Draw how many susceptibles convert to infected this tick */
            int newly = sample_bern_count(susceptible, p_infect);
            if (newly > susceptible) newly = susceptible; /* final safety */

            /* Apply delta; infected cannot exceed alive */
            cell->pop.infected += newly;
            if (cell->pop.infected > cell->pop.total) {
                cell->pop.infected = cell->pop.total;
            }
        }
    }
}

/* --------------------------------------------------------------------------
   Neighbor spread (8-neighborhood)
   -------------------------------------------------------------------------- */
   /* --------------------------------------------------------------------------
      Neighbor spread (8-neighborhood) — one tick
      What this does:
      - Stage cross-cell infections into a buffer (order-independent).
      - For each source cell with I>0, compute base p from β_neighbors * (I / alive),
        scale by diagonal factor, and (if destination is eligible) draw Binomial(S, p).
      - Commit staged adds in a second pass with safety clamps.
   ----------------------------------------------------------------------------*/
void spread_to_neighbors(World* w, const Disease* dz)
{
    if (w == NULL || dz == NULL) return;

    const int H = w->height;
    const int W = w->width;

    /* Persistent staging buffer: holds per-cell additions for this tick only.
       Reason: prevents order bias (we don't modify dst while still reading neighbors). */
    static int* add_buf = NULL;
    static size_t add_cap = 0;               /* capacity in ints */

    size_t need = (size_t)H * (size_t)W;
    if (add_cap < need) {
        /* (Re)allocate to required size; if OOM, safely skip this tick */
        free(add_buf);
        add_buf = (int*)malloc(sizeof(int) * need);
        if (add_buf == NULL) {
            add_cap = 0;
            return; /* out of memory: skip this tick safely */
        }
        add_cap = need;
    }

    /* Clear only the portion we use this tick */
    for (size_t i = 0; i < need; ++i) add_buf[i] = 0;

    /* Neighbor offsets: N, NE, E, SE, S, SW, W, NW */
    static const int DR[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };
    static const int DC[8] = { 0,  1,  1,  1,  0, -1, -1, -1 };

    /* ---------- First pass: compute staged infections ---------- */
    for (int r = 0; r < H; ++r) {
        for (int c = 0; c < W; ++c) {
            const Cell* src = &w->grid[r][c];

            /* Source must be land/port and have living + infected people */
            int src_is_land_or_port = 0;
            if (src->terrain == TERRAIN_LAND) src_is_land_or_port = 1;
            if (src->raw == 'P')              src_is_land_or_port = 1;
            if (src_is_land_or_port == 0)     continue;

            if (src->pop.total <= 0) continue;
            if (src->pop.infected <= 0) continue;

            /* Source pressure: I / alive */
            double pressure = safe_div((double)src->pop.infected, (double)src->pop.total);

            /* Land-use scaling for neighbor spread (kN) */
            double kW_unused = 1.0, kN = 1.0;
            landuse_multipliers(src, &kW_unused, &kN);

            /* Effective neighbor transmission rate (includes death suppression) */
            double b_eff_base = effective_beta_neighbors(src, dz) * kN;
            b_eff_base = clamp_prob(b_eff_base);

            /* Try each of the 8 neighbors */
            for (int k = 0; k < 8; ++k) {
                int rr = r + DR[k];
                int cc = c + DC[k];
                if (rr < 0 || rr >= H || cc < 0 || cc >= W) continue;

                Cell* dst = &w->grid[rr][cc];

                /* Destination must be land/port with people */
                int dst_is_land_or_port = 0;
                if (dst->terrain == TERRAIN_LAND) dst_is_land_or_port = 1;
                if (dst->raw == 'P')              dst_is_land_or_port = 1;
                if (dst_is_land_or_port == 0)     continue;

                if (dst->pop.total <= 0)          continue;

                /* Closed ports cannot receive infection */
                if (dst->raw == 'P' && dst->port_active == 0) {
                    continue;
                }

                /* Diagonals can be scaled down (e.g., weaker than orthogonals) */
                int is_diagonal = 0;
                if (DR[k] != 0 && DC[k] != 0) is_diagonal = 1;

                double scale = 1.0;
                if (is_diagonal != 0) scale = DIAG_NEIGHBOR_FACTOR;

                /* Per-tick neighbor probability p in [0,1] */
                double p_neighbor = b_eff_base * pressure * scale;
                p_neighbor = clamp01(p_neighbor);

                /* Susceptibles at the destination */
                int susceptible = dst->pop.total - dst->pop.infected;
                if (susceptible < 0) susceptible = 0;
                if (susceptible <= 0) continue;

                /* Stage a Binomial draw for this neighbor pair */
                if (p_neighbor > 0.0) {
                    int newly = sample_bern_count(susceptible, p_neighbor);
                    if (newly > susceptible) newly = susceptible;
                    add_buf[rr * W + cc] += newly;  /* accumulate staged adds */
                }
            }
        }
    }

    /* ---------- Second pass: apply staged infections ---------- */
    for (int r = 0; r < H; ++r) {
        for (int c = 0; c < W; ++c) {
            int add = add_buf[r * W + c];
            if (add <= 0) continue;

            Cell* cell = &w->grid[r][c];

            /* Apply only to land/ports */
            int is_land_or_port = 0;
            if (cell->terrain == TERRAIN_LAND) is_land_or_port = 1;
            if (cell->raw == 'P')              is_land_or_port = 1;
            if (is_land_or_port == 0)          continue;

            /* Clamp to available susceptibles at destination */
            int susceptible = cell->pop.total - cell->pop.infected;
            if (susceptible < 0) susceptible = 0;
            if (add > susceptible) add = susceptible;

            /* Final apply; infected cannot exceed total */
            if (add > 0) {
                cell->pop.infected += add;
                if (cell->pop.infected > cell->pop.total) {
                    cell->pop.infected = cell->pop.total;
                }
            }
        }
    }
}

/* --------------------------------------------------------------------------
   Ship helpers
   -------------------------------------------------------------------------- */
int can_spawn_from_port(const Cell* c, const Disease* dz) {
    if (c == NULL || dz == NULL) return 0;
    if (c->raw != 'P')           return 0;
    if (c->pop.total <= 0)       return 0;

    if (port_is_inactive_by_threshold(c, dz) != 0) {
        return 0;
    }
    return 1;
}

int can_infect_on_arrival(const Cell* dest, const Disease* dz) {
    if (dest == NULL || dz == NULL) return 0;
    if (dest->pop.total <= 0)       return 0;

    if (dest->raw == 'P') {
        if (port_is_inactive_by_threshold(dest, dz) != 0) {
            return 0;
        }
    }
    return 1;
}

/* --------------------------------------------------------------------------
   Symptoms → effective parameters (no compounding)
   -------------------------------------------------------------------------- */
static double clampd(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void disease_refresh_effective_params_from_symptoms(Disease* dz) {
    if (dz == NULL) return;

    /* 1) Sum effects with neutral climate (no hot/cold bonus) */
    double dW = 0.0, dN = 0.0, dMu = 0.0;
    int dAware = 0; /* tracked but unused here */
    symptoms_effect_totals(&dz->symptoms,
        NEUTRAL_CLIMATE_FOR_REFRESH,
        &dW, &dN, &dMu, &dAware);

    /* 2) Snapshot baseline on first call */
    static int    have_baseline = 0;
    static double baseW = 0.0, baseN = 0.0, baseMu = 0.0;
    if (have_baseline == 0) {
        baseW = dz->beta_within;
        baseN = dz->beta_neighbors;
        baseMu = dz->mu_mortality;
        have_baseline = 1;
    }

    /* 3) Rebuild (clamped) */
    dz->beta_within = clampd(baseW + dW * dz->symptom_mult_within, 0.0, 1.0);
    dz->beta_neighbors = clampd(baseN + dN * dz->symptom_mult_neighbor, 0.0, 1.0);
    dz->mu_mortality = clampd(baseMu + dMu * dz->symptom_mult_mortality, 0.0, 1.0);
}

/* --------------------------------------------------------------------------
   Initial defaults (driven by macros in disease.h)
   -------------------------------------------------------------------------- */
void disease_init_defaults(Disease* dz) {
    if (dz == NULL) return;

    dz->beta_within = DISEASE_DEFAULT_BETA_WITHIN;
    dz->beta_neighbors = DISEASE_DEFAULT_BETA_NEIGHBORS;
    dz->gamma_recover = DISEASE_DEFAULT_GAMMA_RECOVER;
    dz->mu_mortality = DISEASE_DEFAULT_MU_MORTALITY;
    dz->death_supp_k = DISEASE_DEFAULT_DEATH_SUPP_K;
    dz->port_shutdown_pct = DISEASE_DEFAULT_PORT_SHUTDOWN_PCT;

    /* RNG seed: either fixed (reproducible) or current time. */
    dz->rng_seed = (unsigned)time(NULL);

    dz->symptom_mult_within = DISEASE_DEFAULT_SYMPTOM_MULT_WITHIN;
    dz->symptom_mult_neighbor = DISEASE_DEFAULT_SYMPTOM_MULT_NEIGHBOR;
    dz->symptom_mult_mortality = DISEASE_DEFAULT_SYMPTOM_MULT_MORTALITY;

    dz->mutations_enabled = DISEASE_DEFAULT_MUTATIONS_ENABLED;
}
