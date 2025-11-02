#include <math.h>          /* sqrt, pow */
#include "ports.h"
#include "sea_path.h"
#include "config.h"
#include "rng.h"

/* ---------------------------------------------------------------------------
   Ports module (debiasing):
   - Collect & shuffle ports
   - For each source port, pick up to MAX_ROUTES_PER_PORT destinations
     using a gravity model:
         weight ∝ dest_pop / (distance^GRAVITY_ALPHA)
   - Deduplicate routes and respect MAX_ROUTES
   - Paths are found with find_sea_path(...)
   --------------------------------------------------------------------------- */

   /* ============================== Tunable Defines ============================== */
   /* Avoid zero distance (same tile or numerical quirks). */
#define MIN_ROUTE_DISTANCE            1.0

/* Attempts budget per source when hunting distinct, valid destinations.
   Original logic used: 8 * MAX_ROUTES_PER_PORT, with a floor of 8. */
#define ATTEMPTS_PER_ROUTE_MULT       8
#define MIN_ATTEMPTS_PER_SOURCE       8

   /* Initial "no previous position" marker for ShipRoute.prev_{x,y}. */
#define NO_PREV_COORD                (-1)

/* ============================== Module State ================================= */
/* Global array of all routes (capacity set in config.h). */
static ShipRoute g_routes[MAX_ROUTES];
/* Number of active routes currently stored. */
static int g_route_count = 0;

/* Public accessors (simple and explicit). */
int routes_count(void) { return g_route_count; }
ShipRoute* routes_data(void) { return g_routes; }

/* ============================== Local Types ================================== */
/* Lightweight reference to a port in the grid (coordinates + cell pointer). */
typedef struct {
    int x;
    int y;
    const Cell* cell;   /* points into world grid */
} PortRef;

/* ============================== Helpers ====================================== */
/* Collect all 'P' cells into a flat list (by raw glyph comparison). */
static int collect_ports(const World* w, PortRef* out, int max_out)
{
    int count = 0;

    for (int y = 0; y < w->height; ++y) {
        for (int x = 0; x < w->width; ++x) {
            const Cell* c = &w->grid[y][x];

            if (c->raw == MAP_PORT_CHAR) {
                if (count < max_out) {
                    out[count].x = x;
                    out[count].y = y;
                    out[count].cell = c;
                    count += 1;
                }
                /* If there are more than max_out ports, we just stop storing
                   new ones; route generation below respects MAX_PORTS anyway. */
            }
        }
    }
    return count;
}

/* Fisher–Yates shuffle: unbiased permutation to remove scan-order bias. */
static void shuffle_ports(PortRef* a, int n)
{
    for (int i = n - 1; i > 0; --i) {
        unsigned u = rng_u32();
        int j = (int)(u % (unsigned)(i + 1));

        PortRef t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

/* Euclidean distance between ports (grid pixels).
   Returns at least MIN_ROUTE_DISTANCE to avoid divide-by-zero downstream. */
static double route_distance(int x1, int y1, int x2, int y2)
{
    int dx = x2 - x1;
    int dy = y2 - y1;

    /* Use double during computation for stability. */
    double d2 = (double)dx * (double)dx + (double)dy * (double)dy;
    if (d2 <= 0.0) {
        return MIN_ROUTE_DISTANCE;
    }
    return sqrt(d2);
}

/* Does a route already exist between these endpoints (either direction)? */
static int route_exists(int x1, int y1, int x2, int y2)
{
    for (int i = 0; i < g_route_count; ++i) {
        const ShipRoute* r = &g_routes[i];

        /* same_forward: A->B */
        int same_forward = 0;
        if (r->x1 == x1) {
            if (r->y1 == y1) {
                if (r->x2 == x2) {
                    if (r->y2 == y2) {
                        same_forward = 1;
                    }
                }
            }
        }

        /* same_reverse: B->A */
        int same_reverse = 0;
        if (r->x1 == x2) {
            if (r->y1 == y2) {
                if (r->x2 == x1) {
                    if (r->y2 == y1) {
                        same_reverse = 1;
                    }
                }
            }
        }

        if (same_forward != 0 || same_reverse != 0) {
            return 1;
        }
    }
    return 0;
}

/* Compute gravity-model weight for src→dst.
   Formula (kept identical to original):
       pop = total + dead; if pop < 1 => pop = 1
       d   = route_distance(...)
       w   = pop / (d^alpha)
   Return value is clamped to ≥ 0 (defensive; should already be non-negative).
*/
static double route_weight(const PortRef* ports, int src_idx, int dst_idx, double alpha)
{
    const Cell* dst_cell = ports[dst_idx].cell;

    int pop = dst_cell->pop.total + dst_cell->pop.dead;
    if (pop < 1) {
        pop = 1;
    }

    double d = route_distance(ports[src_idx].x, ports[src_idx].y,
        ports[dst_idx].x, ports[dst_idx].y);

    double penal = pow(d, alpha);
    double w = (double)pop;

    if (penal > 0.0) {
        w = w / penal;
    }
    if (w < 0.0) {
        w = 0.0;
    }
    return w;
}

/* Weighted pick: probability ∝ dest_pop / (distance^alpha)
   Steps:
     1) Compute total weight over all destinations != src.
     2) If total == 0, fallback to uniform pick among others.
     3) Otherwise sample with cumulative sum against target ∈ [0, total).

   Returns: index of chosen destination in [0, n), guaranteed != src_idx (by logic).
*/
static int pick_destination_index(const PortRef* ports, int n, int src_idx, double alpha)
{
    /* 1) total weight */
    double total = 0.0;

    for (int i = 0; i < n; ++i) {
        if (i == src_idx) {
            continue;
        }
        total += route_weight(ports, src_idx, i, alpha);
    }

    /* 2) fallback uniform if total is zero (no information) */
    if (total <= 0.0) {
        unsigned u = rng_u32();
        int j = (int)(u % (unsigned)(n - 1));
        if (j >= src_idx) {
            j += 1; /* skip src */
        }
        return j;
    }

    /* 3) sample proportionally via cumulative sum */
    double r = rng01();
    double target = r * total;
    double cum = 0.0;

    for (int i = 0; i < n; ++i) {
        if (i == src_idx) {
            continue;
        }

        cum += route_weight(ports, src_idx, i, alpha);
        if (cum >= target) {
            return i;
        }
    }

    /* Numerical tail case: pick the last valid index (not src) */
    int last = n - 1;
    if (last == src_idx) {
        last -= 1;
    }
    if (last < 0) {
        last = 0;
    }
    return last;
}

/* ============================== Route Generation ============================== */
/* Build all candidate routes honoring MAX_ROUTES and MAX_ROUTES_PER_PORT.
   Debiasing:
     - Ports are shuffled before processing so "early" ports don't always win.
     - Each source port tries up to ATTEMPTS_PER_ROUTE_MULT * MAX_ROUTES_PER_PORT
       random weighted destinations to get distinct, pathable pairs.
*/
void generate_port_routes(const World* w)
{
    /* 0) reset global route list */
    g_route_count = 0;

    /* 1) collect and shuffle ports */
    PortRef plist[MAX_PORTS];
    int pcount = collect_ports(w, plist, MAX_PORTS);
    if (pcount <= 1) {
        return; /* nothing to connect */
    }
    shuffle_ports(plist, pcount);

    /* 2) for each source, build up to MAX_ROUTES_PER_PORT routes */
    for (int s = 0; s < pcount; ++s) {
        int built_for_src = 0;

        /* Attempts budget: try several times to find distinct, valid destinations. */
        int max_attempts = ATTEMPTS_PER_ROUTE_MULT * MAX_ROUTES_PER_PORT;
        if (max_attempts < MIN_ATTEMPTS_PER_SOURCE) {
            max_attempts = MIN_ATTEMPTS_PER_SOURCE;
        }

        int attempts = 0;
        while (built_for_src < MAX_ROUTES_PER_PORT && attempts < max_attempts) {
            attempts += 1;

            int d = pick_destination_index(plist, pcount, s, (double)GRAVITY_ALPHA);
            if (d == s) {
                /* Should not happen (pick avoids src), but guard anyway. */
                continue;
            }

            int x1 = plist[s].x;
            int y1 = plist[s].y;
            int x2 = plist[d].x;
            int y2 = plist[d].y;

            /* avoid duplicates in either direction */
            if (route_exists(x1, y1, x2, y2) != 0) {
                continue;
            }

            /* find a sea path between ports */
            RoutePath path;
            int ok = find_sea_path(w, x1, y1, x2, y2, &path);
            if (ok == 0) {
                /* try another destination */
                continue;
            }

            /* build the route (single direction; source list is already randomized) */
            if (g_route_count < MAX_ROUTES) {
                ShipRoute* r = &g_routes[g_route_count];

                r->x1 = x1; r->y1 = y1;
                r->x2 = x2; r->y2 = y2;
                r->path = path;

                /* random cooldown in configured range */
                int span = SHIP_BASE_COOLDOWN_MAX - SHIP_BASE_COOLDOWN_MIN + 1;
                if (span < 1) {
                    span = 1;
                }
                r->cooldown = SHIP_BASE_COOLDOWN_MIN + (int)(rng_u32() % (unsigned)span);
                r->ticks_remaining = 0;

                /* initial per-route state */
                r->is_infected = 0;
                r->progress = 0;
                r->active = 0;
                r->prev_x = NO_PREV_COORD;
                r->prev_y = NO_PREV_COORD;

                g_route_count += 1;
                built_for_src += 1;
            }
            else {
                /* out of global route budget */
                break;
            }
        }

        if (g_route_count >= MAX_ROUTES) {
            break;
        }
    }
}
