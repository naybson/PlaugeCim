#ifndef END_SCREEN_H
#define END_SCREEN_H

#include "end_condition.h"  /* EndReason */
#include "disease.h"        /* Disease typedef */
#include "config.h"         /* SimConfig typedef */

void end_screen_set_save_context(const Disease* dz, const SimConfig* cfg);
void end_screen_show(EndReason why, int tick);
void end_screen_show_interactive(EndReason why, int tick);

#endif
