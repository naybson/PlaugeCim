#ifndef HUD_H
#define HUD_H

/* Forward declarations (avoid pulling heavy headers here) */
struct World;              // from world.h
struct Disease;            // from disease.h
struct CureState;          // from cure.h

/* HUD statistics snapshot of the whole world */
typedef struct HudStats {
    unsigned long long total_pop;
    unsigned long long alive_pop;
    unsigned long long infected_pop;
    unsigned long long dead_pop;
    double pct_alive;
    double pct_infected;
    double pct_dead;
} HudStats;

/* Draws the static title/header once at the top of the screen */
void draw_static_header(const struct World* w);

/* Scans the grid and fills HudStats with population counts/percentages */
void compute_world_stats(const struct World* w, HudStats* out);

/* Draws the main HUD line, awareness/cure bars, and population snapshot */
void draw_hud_line(const struct World* w, int tick,
    const struct Disease* dz,
    const struct CureState* cs);

/* Draws the currently active symptoms below the HUD */
void draw_symptoms_line(const struct World* w, const struct Disease* dz);

#endif /* HUD_H */
