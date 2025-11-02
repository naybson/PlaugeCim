#ifndef GAME_DEFS_H
#define GAME_DEFS_H

/* ===== Frame rate ===== */
#ifndef TARGET_FPS
#define TARGET_FPS 60
#endif

/* ===== Map symbols ===== */
#define MAP_SEA_CHAR     ' '
#define MAP_RURAL_CHAR   '*'
#define MAP_URBAN_CHAR   '#'
#define MAP_PORT_CHAR    'P'

/* ===== World population defaults ===== */
#define DEFAULT_CELL_POP        1000
#define URBAN_POP_THRESHOLD     150000   /* threshold for 'urban' */

/* ===== Land-use multipliers =====
   Urban: slower within, easier to neighbors.
   Rural: faster within, harder to neighbors. */
#define URBAN_WITHIN_K          0.70
#define URBAN_NEIGHBOR_K        1.40
#define RURAL_WITHIN_K          1.25
#define RURAL_NEIGHBOR_K        1.00

   /* ===== Prob clamps ===== */
#define PROB_MIN                0.0
#define PROB_MAX                0.999

/* ===== Climate tints (RGB 0..255) ===== */
#define HOT_R  252  /* FCEBCB */
#define HOT_G  235
#define HOT_B  203
#define COLD_R 203  /* CBFCFC */
#define COLD_G 252
#define COLD_B 252

/* ===== Ports & routes ===== */
#define MAX_ROUTE_PATH 512
#define MAX_PORTS      64
#define MAX_ROUTES     128

/* ===== Route generation (gravity model): weight ∝ pop / distance^ALPHA ===== */
#ifndef GRAVITY_ALPHA
#define GRAVITY_ALPHA 1.2
#endif
#ifndef MAX_ROUTES_PER_PORT
#define MAX_ROUTES_PER_PORT 4
#endif

/* ===== Ships ===== */
#define SHIP_BASE_COOLDOWN_MIN  20
#define SHIP_BASE_COOLDOWN_MAX  49
#define SHIP_LAUNCH_PROB        0.30
#define PORT_INFECTION_PROB     0.60

/* ===== Rendering / visuals ===== */
#ifndef MIN_INF_TINT
#define MIN_INF_TINT 0.2        /* minimum visible red tint if infected > 0 */
#endif

#endif /* GAME_DEFS_H */
