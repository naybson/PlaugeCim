#ifndef SEA_PATH_H
#define SEA_PATH_H
#include "types.h"

/* BFS over sea; allow end cell even if land (port tile) */
int find_sea_path(const World* w, int sx, int sy, int dx, int dy, RoutePath* out);

#endif /* SEA_PATH_H */
