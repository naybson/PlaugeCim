#include <stdio.h>
#include "hud.h"
#include "ansi.h"       // cup(), ERASE_LINE, WRAP_OFF, ANSI_RESET
#include "world.h"
#include "disease.h"
#include "cure.h"       // cure_awareness(), cure_progress(), cure_is_active()
#include "symptoms.h"   // symptoms_build_active_names(...)

// ========== CONSTANTS ==========
#define PERCENT_BAR_WIDTH 24     // characters used for percent bars
#define HUD_ROW_OFFSET     4     // HUD starts 4 rows below map
#define BAR_EXTRA_ROWS     5     // Awareness + Cure bars position
#define POP_ROW_OFFSET     8     // Population line position
#define SYMPTOM_ROW_BASE   9     // Symptoms line base offset
#define TOP_TITLE_ROW    1


// ========== DRAW STATIC HEADER ==========s
/*
   Draws the static header once at the top of the screen.
   Shows simulation title and map dimensions.
*/
void draw_static_header(const struct World* w) {
    fputs(WRAP_OFF, stdout);
    cup(TOP_TITLE_ROW, 1);
    fputs(ERASE_LINE, stdout);
    printf(CSI "1;31mTo zoom out or in, press ctrl+scroll wheel up or down on your mouse!%s",
        ANSI_RESET);
}

// ========== COMPUTE WORLD STATS ==========
/*
   Computes population totals across the whole grid.
   Fills HudStats with total/alive/infected/dead counts and percentages.
*/
void compute_world_stats(const struct World* w, HudStats* out) {
    if (out == NULL) return;

    unsigned long long sum_infected = 0ULL;
    unsigned long long sum_dead = 0ULL;

    if (w != NULL) {
        for (int y = 0; y < w->height; y++) {
            for (int x = 0; x < w->width; x++) {
                const Cell* c = &w->grid[y][x];
                if (c->pop.infected > 0) sum_infected += c->pop.infected;
                if (c->pop.dead > 0)     sum_dead += c->pop.dead;
            }
        }
    }

    out->total_pop = (w ? (unsigned long long)w->totalpop : 0ULL);
    out->infected_pop = sum_infected;
    out->dead_pop = sum_dead;

    // Alive = total - dead (clamped to 0)
    out->alive_pop = (out->total_pop >= out->dead_pop)
        ? out->total_pop - out->dead_pop
        : 0ULL;

    // Percentages
    if (out->total_pop == 0ULL) {
        out->pct_alive = 0.0;
        out->pct_infected = 0.0;
        out->pct_dead = 0.0;
    }
    else {
        out->pct_alive = 100.0 * (double)out->alive_pop / (double)out->total_pop;
        out->pct_infected = 100.0 * (double)out->infected_pop / (double)out->total_pop;
        out->pct_dead = 100.0 * (double)out->dead_pop / (double)out->total_pop;
    }
}

// ========== INTERNAL: PERCENT BAR DRAWER ==========
/*
   Helper: draws a percentage bar with a label.
   Example: [######          ] 45%
*/
static void draw_percent_bar(int row, int col, const char* label, double pct) {
    int filled = (int)((pct / 100.0) * (double)PERCENT_BAR_WIDTH + 0.5);

    if (filled < 0) filled = 0;
    if (filled > PERCENT_BAR_WIDTH) filled = PERCENT_BAR_WIDTH;

    cup(row, col);
    fputs(ERASE_LINE, stdout);

    // Label
    fputs(label, stdout);
    fputs(" [", stdout);

    // Bar
    for (int i = 0; i < PERCENT_BAR_WIDTH; i++) {
        fputc((i < filled) ? '#' : ' ', stdout);
    }

    // Trailing percentage
    printf("] %3.0f%%", pct);
}

// ========== DRAW HUD LINE ==========
/*
   Draws the main HUD below the map:
   - Tick count
   - Spread multipliers
   - Recovery/Mortality rates
   - Awareness and Cure bars
   - Population snapshot
*/
void draw_hud_line(const struct World* w, int tick,
    const struct Disease* dz,
    const struct CureState* cs) {
    HudStats stats;
    compute_world_stats(w, &stats);

    // Effective spread multipliers (0 if no disease)
    double eff_within = (dz ? dz->beta_within * dz->symptom_mult_within : 0.0);
    double eff_neighbor = (dz ? dz->beta_neighbors * dz->symptom_mult_neighbor : 0.0);

    // (1) Top HUD line: tick, spread, recovery, mortality
    cup(HUD_ROW_OFFSET + (w ? w->height : 0), 1);
    fputs(ERASE_LINE, stdout);
    printf("Tick %-6d | Spread (within/neighbor): %.2f / %.2f | Recovery: %.3f | Mortality: %.3f%s",
        tick, eff_within, eff_neighbor,
        (dz ? dz->gamma_recover : 0.0),
        (dz ? dz->mu_mortality : 0.0),
        ANSI_RESET);

    // (2) Awareness + Cure bars
    int base_row = BAR_EXTRA_ROWS + (w ? w->height : 0);
    double awareness = (cs ? cure_awareness(cs) : 0.0);
    double cure = (cs ? cure_progress(cs) : 0.0);

    draw_percent_bar(base_row + 0, 1, "Public Awareness", awareness);
    draw_percent_bar(base_row + 1, 1, "Cure Progress   ", cure);

    cup(base_row + 2, 1);
    fputs(ERASE_LINE, stdout);
    if (cs && cure_is_active(cs)) {
        fputs("Cure Status: ACTIVE: spread halted", stdout);
    }
    else {
        fputs("Cure Status: Researching...", stdout);
    }

    // (3) Population snapshot
    int pop_row = POP_ROW_OFFSET + (w ? w->height : 0);
    cup(pop_row, 1);
    fputs(ERASE_LINE, stdout);
    printf("Population - Alive: %.3f%%   Infected: %.3f%%   Dead: %.3f%%%s",
        stats.pct_alive, stats.pct_infected, stats.pct_dead, ANSI_RESET);
}

// ========== DRAW SYMPTOMS LINE ==========
/*
   Draws the currently active symptoms under the HUD block.
*/
void draw_symptoms_line(const struct World* w, const struct Disease* dz) {
    int row = SYMPTOM_ROW_BASE;
    if (w && w->height > 0) {
        row += w->height;  // Place under population line
    }

    fputs(WRAP_OFF, stdout);
    cup(row, 1);
    fputs(ERASE_LINE, stdout);

    fputs("Symptoms: ", stdout);

    if (!dz) {
        fputs("-", stdout);
        return;
    }

    const char* names[SYM_COUNT];
    int n = symptoms_build_active_names(&dz->symptoms, names, SYM_COUNT);

    if (n <= 0) {
        fputs("-", stdout);
        return;
    }

    for (int i = 0; i < n; i++) {
        fputs(names[i], stdout);
        if (i + 1 < n) fputc(' - ', stdout);
    }
}
