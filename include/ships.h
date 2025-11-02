#ifndef SHIPS_H
#define SHIPS_H
#include "types.h"

/* Progress/launch/arrival + overlay drawing & erasing */
void update_ship_routes(World* w);

/* Reset overlay memory when we invalidate/resize */
void reset_ship_overlays(void);

int ships_consume_first_infected_flag(void);

#endif /* SHIPS_H */
