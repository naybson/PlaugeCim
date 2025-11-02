#ifndef MAP_IO_H
#define MAP_IO_H

#include "types.h"

/*
  Map I/O: load a text map into a World grid.

  File format (one char per cell, constant width per line):
    - ' ' (space), '\t', '\r', '\n', '~' => SEA
    - 'P' => Port (treated like land, urban, with baseline population)
    - '#' => Urban, cold
    - '&' => Urban, hot      (display-normalized to '#')
    - '*' => Rural, hot
    - '^' => Rural, cold
    - Anything else on land => Rural, hot (fallback)

  Public API:
    get_map_dimensions(path, &w, &h) -> scans file once and validates constant line width.
    load_world_from_file(path, world) -> allocates world->grid and populates cells.

  Internals exposed for unit tests:
    parse_cell_from_char(cell, ch)
    allocate_world_grid(world)
    free_world(world)
*/

/* Dimension scan + allocate + populate */
int  get_map_dimensions(const char* path, int* out_w, int* out_h);
int  load_world_from_file(const char* path, World* w);

/* Internals split for testing */
void parse_cell_from_char(Cell* cell, char ch);
int  allocate_world_grid(World* w);
void free_world(World* w);

#endif /* MAP_IO_H */
