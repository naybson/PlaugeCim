#ifndef TYPES_H
#define TYPES_H

#include "game_defs.h"

/* ---------- Basic enums & PODs ---------- */
typedef enum { TERRAIN_SEA, TERRAIN_LAND } Terrain;
typedef enum { CLIMATE_HOT, CLIMATE_COLD } Climate;
typedef enum { SETTLE_RURAL, SETTLE_URBAN } Settlement;

typedef struct {
    int pop_total0;  /* initial population snapshot if you store it */
    int total;       /* alive */
    int infected;    /* subset of total */
    int dead;        /* cumulative dead */
} Population;

typedef struct Cell {
    Terrain    terrain;
    char       raw;          /* map glyph: '*','#','P',' ' */
    Population pop;
    int        port_active;  /* 1 if port can send/receive ships */
    /* sticky bins for flicker-free rendering */
    int  disp_i_bin;
    int  disp_d_bin;
    char disp_glyph;
    Climate    climate;
    Settlement settlement;
} Cell;

typedef struct World {
    int    totalpop;  /* total alive across land/ports (recomputed) */
    int    width;
    int    height;
    Cell** grid;      /* grid[height][width] */
} World;

/* Render cache key (diff-only renderer) */
typedef struct {
    unsigned short i_bin;  /* infection bin */
    unsigned short d_bin;  /* death bin */
    unsigned char  glyph;  /* printable char */
    unsigned char  is_sea; /* 0/1 */
} RenderKey;

/* Route path across sea (BFS output) */
typedef struct {
    int x[MAX_ROUTE_PATH];
    int y[MAX_ROUTE_PATH];
    int length;
} RoutePath;

/* Ship route runtime state */
typedef struct {
    int x1, y1;      /* source port */
    int x2, y2;      /* target port */
    RoutePath path;

    int cooldown;        /* base cooldown ticks */
    int ticks_remaining; /* countdown when inactive */
    int is_infected;     /* current ship infection flag */
    int progress;        /* index along path */
    int active;          /* sailing flag */

    /* overlay pixel to erase on next frame */
    int prev_x;
    int prev_y;
} ShipRoute;

#endif /* TYPES_H */
