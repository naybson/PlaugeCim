#ifndef WORLD_H
#define WORLD_H

#include "types.h"
#include "disease.h"

/*
  World-level operations (high-level game loop helpers):

  - seed_patient_zero:   Infect the first land cell found that has population.
  - step_world:          Advance the simulation by one tick (mortality, ports, infection spread).
  - ensure_port_population:
                         Make sure all 'port' cells are land with a non-zero population.
                         If a port has zero population, it gets default_pop people.
*/

/* Infect the very first populated land cell, if any. Safe to call once at start. */
void seed_patient_zero(World* w);

/* Advance simulation by one full tick (order is important and mirrors original logic). */
void step_world(World* w, const Disease* dz);

/* Ensure every port cell has terrain=LAND and a minimum starting population. */
void ensure_port_population(World* w, int default_pop);

#endif /* WORLD_H */
