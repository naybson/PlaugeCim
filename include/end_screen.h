#ifndef END_SCREEN_H
#define END_SCREEN_H

#include "end_condition.h"  /* EndReason */

/* Forward declarations instead of including heavy headers */
struct Disease;
struct SimConfig;

/* Let main pass disease + cfg so the end screen can offer saving */
void end_screen_set_save_context(const struct Disease* dz,
    const struct SimConfig* cfg);

void end_screen_show(EndReason why, int tick);
void end_screen_show_interactive(EndReason why, int tick);

#endif
