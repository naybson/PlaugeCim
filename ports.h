#ifndef PORTS_H
#define PORTS_H

#include "types.h"

/*
  Ports & Shipping Routes

  What this module does:
    1) Finds all port cells ('P' glyph) in the world.
    2) Shuffles them (to remove scan-order bias).
    3) For each source port, chooses up to MAX_ROUTES_PER_PORT destinations
       using a gravity model:
         weight ∝ dest_population / (distance^GRAVITY_ALPHA)
    4) Deduplicates routes (A→B or B→A counts as one connection).
    5) Builds sea paths with find_sea_path(...).
    6) Stores everything in an internal global array you can read via accessors.

  Public API:
    void        generate_port_routes(const World* w);
    int         routes_count(void);       // how many routes are currently generated
    ShipRoute*  routes_data(void);        // pointer to the internal routes array
*/

/* Build all routes for the current world (overwrites previous list). */
void generate_port_routes(const World* w);

/* Optional accessors to inspect computed routes. */
int        routes_count(void);
ShipRoute* routes_data(void);

#endif /* PORTS_H */
