#include "world.h"
#include "disease.h"
#include <stddef.h>  // for NULL

/*
   World-level operations:
   - Seeding patient zero
   - Advancing world state one tick
   - Ensuring ports are properly initialized
*/

/* ------------------------------ Defines ------------------------------ */
/* Map glyph that designates a port. Keeping it here avoids scattering the char literal. */
#define PORT_GLYPH 'P'

/* Pass-through flag for the mortality step: original code used a literal 1. */
#define WORLD_USE_RECOVERY 1

/* -------------------------- Helper Functions -------------------------- */
/* (None needed here — the file is intentionally small and readable.) */


/* Ensure every port cell has the right terrain and a non-zero population.
   - If a cell is marked with the port glyph, it MUST be land and have people.
   - If the cell's population is zero or negative, we assign default_pop people.
   - Infection/deaths counters are reset for newly initialized ports.
   Why this exists: map data may mark ports by glyph only; this normalizes them
   so later systems (shipping, spread, HUD) can rely on consistent assumptions.
*/
void ensure_port_population(World* w, int default_pop)
{
    if (w == NULL) return;

    for (int y = 0; y < w->height; ++y) {
        for (int x = 0; x < w->width; ++x) {
            Cell* c = &w->grid[y][x];

            if (c->raw == PORT_GLYPH) {
                /* Ports are land tiles with people. Enforce those rules. */
                if (c->terrain != TERRAIN_LAND) {
                    c->terrain = TERRAIN_LAND;
                }

                /* If a port somehow has no living population, initialize it. */
                if (c->pop.total <= 0) {
                    c->pop.total = default_pop;  /* e.g., 150; caller decides the number */
                    c->pop.infected = 0;
                    c->pop.dead = 0;
                }
            }
        }
    }
}

/* Infect the very first land cell found that already has population.
   Notes:
   - We do not “hunt” for the best cell — first match wins (keeps logic identical).
   - If the chosen cell already has some infection, we leave it (or bump to 1 if 0). */
void seed_patient_zero(World* w) {
    if (w == NULL) return;

    for (int y = 0; y < w->height; ++y) {
        for (int x = 0; x < w->width; ++x) {
            Cell* c = &w->grid[y][x];

            if (c->terrain == TERRAIN_LAND && c->pop.total > 0) {
                if (c->pop.infected == 0) {
                    c->pop.infected = 1;  // start infection here
                }
                return;  // only seed once
            }
        }
    }
}

/* Advance the world by one epidemic tick.
   The order here is intentional and matches the original behavior:

   1) Mortality (and recoveries if enabled):
        - apply_mortality_world() changes population counts based on disease.
        - WORLD_USE_RECOVERY keeps the original literal '1' behavior, just named.

   2) Port status updates:
        - update_all_ports() may enable/disable ports depending on death rates, etc.
        - Downstream systems rely on ports being in the correct active state.

   3) Infection dynamics:
        - update_cell_infections(): intra-cell infection updates (within the same tile).
        - spread_to_neighbors():    inter-cell spread to adjacent tiles.

   Keeping this order preserves the original simulation semantics.
*/
void step_world(World* w, const Disease* dz) {
    if (w == NULL || dz == NULL) return;

    apply_mortality_world(w, dz, WORLD_USE_RECOVERY);  // 1) deaths/recovery
    update_all_ports(w, dz);                           // 2) port active/inactive
    update_cell_infections(w, dz);                     // 3) within-cell spread
    spread_to_neighbors(w, dz);                        // 4) cross-cell spread
}