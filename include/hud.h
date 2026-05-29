#ifndef HUD_H
#define HUD_H

#include "cure.h"     /* CureState typedef */
#include "disease.h"  /* Disease typedef */
#include "world.h"    /* World typedef */

typedef struct HudStats {
    unsigned long long total_pop;
    unsigned long long alive_pop;
    unsigned long long infected_pop;
    unsigned long long dead_pop;
    double pct_alive;
    double pct_infected;
    double pct_dead;
} HudStats;

void draw_static_header(const World* w);
void compute_world_stats(const World* w, HudStats* out);
void draw_hud_line(const World* w, int tick, const Disease* dz, const CureState* cs);
void draw_symptoms_line(const World* w, const Disease* dz);

#endif /* HUD_H */
