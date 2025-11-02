#include <stdio.h>
#include "ships.h"
#include "ports.h"
#include "render.h"
#include "ansi.h"
#include "utils.h"
#include "rng.h"
#include "config.h"
#include "turnpoints.h"  
#include "hud.h"        



/*
   Ship route simulation:
   - Move ships along precomputed routes
   - Infect destination ports if carrying disease
   - Overlay ships on top of world rendering
*/
/* First infected ship detection (simple one-shot latch) */
static int g_first_infected_ship_seen = 0;

int ships_consume_first_infected_flag(void)
{
    if (g_first_infected_ship_seen != 0) {
        g_first_infected_ship_seen = 0;  /* consume */
        return 1;
    }
    return 0;
}

static Cell* endpoint_port_cell(World* w, int x, int y)
{
    /* Bounds clamp */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= w->width)  x = w->width - 1;
    if (y >= w->height) y = w->height - 1;

    Cell* here = &w->grid[y][x];
    if (here->raw == 'P') {
        return here;
    }

    int dy = -1;
    while (dy <= 1) {
        int dx = -1;
        while (dx <= 1) {
            int nx = x + dx;
            int ny = y + dy;
            if (nx >= 0 && nx < w->width && ny >= 0 && ny < w->height) {
                Cell* nb = &w->grid[ny][nx];
                if (nb->raw == 'P') {
                    return nb;
                }
            }
            dx += 1;
        }
        dy += 1;
    }
    /* Fallback: no adjacent 'P' found */
    return here;
}

void update_ship_routes(World* w)
{
    int n = routes_count();
    ShipRoute* routes = routes_data();

    int i = 0;
    while (i < n) {
        ShipRoute* r = &routes[i];

        /* --- Active ship (sailing) --- */
        if (r->active) {
            /* If destination became closed/empty mid-voyage, cancel the ship now */
            Cell* dst_port = endpoint_port_cell(w, r->x2, r->y2);
            int dest_ok = 1;
            if (dst_port->raw == 'P') {
                if (dst_port->pop.total <= 0) {
                    dest_ok = 0;
                }
                else if (dst_port->port_active == 0) {
                    dest_ok = 0;
                }
            }
            /* If destination is not OK, abort voyage and reset */
            if (dest_ok == 0) {
                if (r->prev_x >= 0 && r->prev_y >= 0) {
                    restore_cell_from_cache(w, r->prev_x, r->prev_y);
                    r->prev_x = -1;
                    r->prev_y = -1;
                }
                r->active = 0;
                r->progress = 0;
                r->ticks_remaining = r->cooldown;
                i += 1;
                continue;
            }

            if (r->progress < r->path.length) {
                /* Erase previous overlay if drawn */
                if (r->prev_x >= 0 && r->prev_y >= 0) {
                    restore_cell_from_cache(w, r->prev_x, r->prev_y);
                }

                int x = r->path.x[r->progress];
                int y = r->path.y[r->progress];

                /* Draw ship overlay at new position */
                cup(2 + y, 1 + x);
                if (r->is_infected) {
                    printf("%sO%s", "\x1b[38;5;196m", ANSI_RESET);  /* red 'O' */
                }
                else {
                    printf("%sO%s", sea_color(), ANSI_RESET);       /* blue 'O' */
                }

                r->prev_x = x;
                r->prev_y = y;
                r->progress += 1;
            }
            else {
                /* --- Ship reached destination --- */

                /* Erase final overlay */
                if (r->prev_x >= 0 && r->prev_y >= 0) {
                    restore_cell_from_cache(w, r->prev_x, r->prev_y);
                    r->prev_x = -1;
                    r->prev_y = -1;
                }

                /* Infect destination if infected ship and destination can receive */
                Cell* dst = endpoint_port_cell(w, r->x2, r->y2);
                if (r->is_infected) {
                    int can_receive = 1;
                    if (dst->pop.total <= 0) {
                        can_receive = 0;
                    }
                    if (can_receive != 0) {
                        if (dst->raw == 'P' && dst->port_active == 0) {
                            can_receive = 0;
                        }
                    }
                    if (can_receive != 0) {
                        if (dst->pop.infected < dst->pop.total) {
                            /* Always seed one infection on arrival */
                            dst->pop.infected += 1;
                            if (dst->pop.infected > dst->pop.total) {
                                dst->pop.infected = dst->pop.total;
                            }
                        }
                    }
                }

                /* Reset route state */
                r->active = 0;
                r->progress = 0;
                r->ticks_remaining = r->cooldown;
            }

            i += 1;
            continue;
        }

        /* --- Inactive route: waiting for next ship --- */
        if (r->ticks_remaining > 0) {
            r->ticks_remaining -= 1;
            i += 1;
            continue;
        }

        /* Evaluate source and destination ports for launch */
        Cell* src = endpoint_port_cell(w, r->x1, r->y1);
        Cell* dst = endpoint_port_cell(w, r->x2, r->y2);

        /* Source must be able to send */
        int can_spawn = 1;
        if (src->raw == 'P') {
            if (src->pop.total <= 0) {
                can_spawn = 0;
            }
            else if (src->port_active == 0) {
                can_spawn = 0;
            }
        }
        else {
            /* Non-port land still must have people */
            if (src->pop.total <= 0) {
                can_spawn = 0;
            }
        }

        /* Destination must be able to receive (if it's a port) */
        int dest_ok = 1;
        if (dst->raw == 'P') {
            if (dst->pop.total <= 0) {
                dest_ok = 0;
            }
            else if (dst->port_active == 0) {
                dest_ok = 0;
            }
        }

        if (can_spawn == 0 || dest_ok == 0) {
            r->ticks_remaining = r->cooldown;
            r->active = 0;
            r->progress = 0;
            r->prev_x = -1;
            r->prev_y = -1;
            i += 1;
            continue;
        }

        /* Scale launch chance by source alive fraction^2 */
        double launch_prob = SHIP_LAUNCH_PROB;
        int init_pop = src->pop.total + src->pop.dead;
        if (init_pop > 0) {
            double alive_frac = (double)src->pop.total / (double)init_pop;
            double scale = alive_frac * alive_frac;  /* stronger slowdown as port dies */
            if (scale < 0.0) scale = 0.0;
            if (scale > 1.0) scale = 1.0;
            launch_prob = launch_prob * scale;
        }
        else {
            launch_prob = 0.0;
        }

        /* Decide whether to launch ship this tick */
        double u = rng01();
        if (u < launch_prob) {
            r->active = 1;
            r->progress = 0;
            r->prev_x = -1;
            r->prev_y = -1;

            /* Ship infection state depends on source population */
            r->is_infected = 0;
            if (src->pop.infected > 0) {
                r->is_infected = 1;

                if (r->is_infected == 1 && g_first_infected_ship_seen == 0) {
                    g_first_infected_ship_seen = 1;
                }
            }
        }
        else {
            r->ticks_remaining = r->cooldown;
        }

        i += 1;
    }
}

/* Reset ship overlays (after full redraw) */
void reset_ship_overlays(void) {
    int n = routes_count();
    ShipRoute* routes = routes_data();
    for (int i = 0; i < n; ++i) {
        routes[i].prev_x = -1;
        routes[i].prev_y = -1;
    }
}
