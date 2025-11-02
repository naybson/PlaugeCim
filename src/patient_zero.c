// patient_zero.c — modal picker for Patient Zero (resize-safe, low flicker)
//
// What this module does:
// - Lets the player choose the starting cell (patient zero) using arrow keys.
// - Shows a bracket cursor overlay "[ ]" on top of the map without redrawing everything.
// - Survives console zoom/resize: we rebuild the base frame and redraw the hint.
// - Enforces a legal starting cell: Land or Port with at least one living person.
//
// Design notes:
// - We rely on render_cell_to_screen(...) for exact screen positions,
//   so the cursor tracks whatever layout the renderer used in the last frame.
// - We avoid flicker by drawing the base frame only when needed, and the overlay often.
// - Magic numbers are centralized as #defines up top.
// - No conditional operator (?:) anywhere—plain ifs for clarity.

#include <stdio.h>
#include <stdlib.h>   /* system("cls") on Windows */
#ifdef _WIN32
#include <conio.h>    /* _getch, _kbhit */
#endif

#include "ansi.h"           /* cup(), HIDE_CURSOR, SHOW_CURSOR, ANSI_RESET, WRAP_OFF */
#include "render.h"         /* render_cell_to_screen(), invalidate_frame_cache(), draw_frame_incremental() */
#include "console_win.h"    /* ensure_console_buffer_at_least(), check_console_resize() */
#include "utils.h"          /* sleep_ms(...) */
#include "patient_zero.h"
#include "world.h"
#include "disease.h"

/* ------------------------------------------------------------------------- */
/* Tunable constants (one place to tweak)                                    */
/* ------------------------------------------------------------------------- */
#define P0_IDLE_SLEEP_MS            16   /* idle poll delay when no key is pressed (~60 FPS) */
#define P0_HINT_OFFSET_ROWS          2   /* rows below the last map row to print the hint */
#define P0_MSG_OFFSET_ROWS           3   /* rows below the last map row to print errors */
#define P0_EXTRA_BUFFER_ROWS        12   /* extra console rows beyond map height (for HUD + hint) */

/* ------------------------------------------------------------------------- */
/* Local key codes used by the picker                                        */
/* ------------------------------------------------------------------------- */
enum {
    KEY_NONE = 0,
    KEY_LEFT = 1,
    KEY_RIGHT = 2,
    KEY_UP = 3,
    KEY_DOWN = 4,
    KEY_ESC = 5,
    KEY_ENTER = 6,
    KEY_SPACE = 7
};

/* Remove the previous "[ ]" by restoring the affected cells from the cache.
   We restore: the previous cell itself, plus its left/right neighbors
   (since we printed at sx-1 and sx+1). */
static void p0_erase_cursor_brackets(const World* w, int cx, int cy) {
    if (w == NULL) return;

    if (cy >= 0 && cy < w->height) {
        if (cx >= 0 && cx < w->width) {
            restore_cell_from_cache(w, cx, cy);
        }
        if (cx - 1 >= 0 && cx - 1 < w->width) {
            restore_cell_from_cache(w, cx - 1, cy);
        }
        if (cx + 1 >= 0 && cx + 1 < w->width) {
            restore_cell_from_cache(w, cx + 1, cy);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* read_key_blocking                                                         */
/* Reads one key press and translates it into our local KEY_* codes.         */
/* Windows path uses _getch(); POSIX path parses simple ANSI arrow sequences.*/
static int read_key_blocking(void)
{
    int ch = _getch();
    if (ch == 27) {
        return KEY_ESC;
    }
    if (ch == '\r' || ch == '\n') {
        return KEY_ENTER;
    }
    if (ch == ' ') {
        return KEY_SPACE;
    }
    if (ch == 0 || ch == 224) {
        int ch2 = _getch();
        if (ch2 == 75) {
            return KEY_LEFT;   /* ← */
        }
        if (ch2 == 77) {
            return KEY_RIGHT;  /* → */
        }
        if (ch2 == 72) {
            return KEY_UP;     /* ↑ */
        }
        if (ch2 == 80) {
            return KEY_DOWN;   /* ↓ */
        }
    }
    return KEY_NONE;
}

/* ------------------------------------------------------------------------- */
/* read_key_nonblocking                                                      */
/* Windows-friendly non-blocking poll. On POSIX we return KEY_NONE and       */
/* block in the main loop to keep things simple.                              */
static int read_key_nonblocking(void)
{
    if (_kbhit() != 0) {
        return read_key_blocking();
    }
    return KEY_NONE;
}

/* ------------------------------------------------------------------------- */
/* p0_cell_to_screen                                                         */
/* Convert map cell (cx,cy) → screen coords (sx,sy) using the renderer’s     */
/* current layout (updated during the last incremental draw).                 */
static void p0_cell_to_screen(const World* w, int cx, int cy, int* sx, int* sy)
{
    render_cell_to_screen(w, cx, cy, sx, sy);
}
/* Is (x,y) a legal Patient Zero start?
   Rules:
   - Must be inside the map bounds.
   - Must be on LAND or on a raw 'P' (port) cell.
   - Must have at least one living person (pop.total > 0).
   Returns 1 = valid, 0 = invalid. */
int p0_cell_is_valid_start(const World* w, int x, int y)
{
    /* Defensive: need a world, and the coordinates must be in range */
    if (w == NULL) return 0;
    if (x < 0) return 0;
    if (y < 0) return 0;
    if (x >= w->width) return 0;
    if (y >= w->height) return 0;

    /* Read the target cell once */
    const Cell* c = &w->grid[y][x];

    /* --- Terrain rule: allow Land or Port --- */
    {
        int is_land = 0;
        int is_port = 0;

        /* LAND cells are always allowed */
        if (c->terrain == TERRAIN_LAND) {
            is_land = 1;
        }
        /* Ports are recognized by the raw glyph 'P' (treated as land) */
        if (c->raw == 'P') {
            is_port = 1;
        }

        /* If it's neither Land nor Port → not a valid start (e.g., sea) */
        if (is_land == 0 && is_port == 0) {
            return 0;
        }
    }

    /* --- Population rule: need someone to infect --- */
    if (c->pop.total <= 0) {
        return 0;   /* empty cell: cannot seed Patient Zero here */
    }
    
    return 1; /* Passed all checks */
}

/* ------------------------------------------------------------------------- */
/* world_seed_patient_zero                                                   */
/* Infect exactly one person in the chosen cell.                              */
/* NOTE: If 'total' means susceptible count, decrementing it is correct.      */
/* If 'total' is fixed population, remove the decrement (keep your logic).    */
void world_seed_patient_zero(World* w, Disease* dz, int x, int y)
{
    (void)dz; /* reserved for future hooks */
    if (w == NULL) return;
    if (x < 0) return;
    if (y < 0) return;
    if (x >= w->width) return;
    if (y >= w->height) return;

    Cell* c = &w->grid[y][x];
    if (c->pop.total < 1) {
        return;
    }

    c->pop.total -= 1;      /* remove this line if 'total' is constant */
    c->pop.infected += 1;
}

/* ------------------------------------------------------------------------- */
/* p0_draw_cursor_brackets                                                   */
/* Draw the "[ ]" overlay around the selected cell. Hint is drawn separately */
/* after base redraws to avoid flicker.                                      */
static void p0_draw_cursor_brackets(const World* w, int cx, int cy)
{
    int sx = 0;
    int sy = 0;
    p0_cell_to_screen(w, cx, cy, &sx, &sy);

    /* Safety: avoid printing left bracket before column 1 */
    if (sx > 1) {
        cup(sy, sx - 1);
        fputc('[', stdout);
    }

    cup(sy, sx + 1);
    fputc(']', stdout);
}

/* ------------------------------------------------------------------------- */
/* p0_draw_hint_line                                                         */
/* Print the one-line instructions a couple of rows under the map.           */
static void p0_draw_hint_line(const World* w)
{
    int last_sy = 0; /* screen row of the last map row */
    render_cell_to_screen(w, 0, w->height - 1, NULL, &last_sy);

    /* position the hint text just under the map */
    {
        int hint_y = last_sy + P0_HINT_OFFSET_ROWS;
        if (hint_y < 1) {
            hint_y = 1;
        }
        cup(hint_y, 2);
        fputs("Pick Patient Zero  (Use ctrl+Scroll wheel to zoom out!)|  Arrows: move   Enter/Space: choose   ESC: back to Setup", stdout);
    }
}

/* ------------------------------------------------------------------------- */
/* p0_handle_resize                                                          */
/* On zoom/resize: clear the screen, ensure console buffer size, and          */
/* invalidate the render cache so the next incremental draw repaints all.     */
static void p0_handle_resize(const World* w)
{
#ifdef _WIN32
    system("cls");
#else
    fputs("\x1b[2J\x1b[H", stdout); /* clear + home */
#endif

#ifdef WRAP_OFF
    fputs(WRAP_OFF, stdout);        /* prevent wrapped lines after zoom */
#endif

    /* Make sure we have enough rows under the map for HUD + hints */
    ensure_console_buffer_at_least(w->width, w->height + P0_EXTRA_BUFFER_ROWS);

    /* Force next frame to repaint all cells (no stale cache artifacts) */
    invalidate_frame_cache(w);
}

/* ------------------------------------------------------------------------- */
/* p0_snap_to_nearest                                                        */
/* If (cx,cy) is not legal, search outward in diamond/Manhattan rings for     */
/* the nearest legal cell and snap to it.                                     */
static void p0_snap_to_nearest(const World* w, int* cx, int* cy)
{
    if (w == NULL) return;
    if (cx == NULL) return;
    if (cy == NULL) return;

    /* Already valid? Done. */
    if (p0_cell_is_valid_start(w, *cx, *cy) != 0) {
        return;
    }

    /* Fallback search radius up to the larger map dimension */
    int best_x = *cx;
    int best_y = *cy;
    int max_r = w->width;
    if (w->height > max_r) {
        max_r = w->height;
    }

    int r = 1;
    while (r <= max_r) {
        int dy = -r;
        while (dy <= r) {
            /* abs_dy = absolute value of dy, written without ?: */
            int abs_dy = dy;
            if (abs_dy < 0) {
                abs_dy = -abs_dy;
            }

            int dx = r - abs_dy;

            int x1 = *cx - dx;
            int y1 = *cy + dy;
            int x2 = *cx + dx;
            int y2 = *cy + dy;

            if (x1 >= 0 && x1 < w->width && y1 >= 0 && y1 < w->height) {
                if (p0_cell_is_valid_start(w, x1, y1) != 0) {
                    best_x = x1;
                    best_y = y1;
                    *cx = best_x;
                    *cy = best_y;
                    return;
                }
            }
            if (x2 >= 0 && x2 < w->width && y2 >= 0 && y2 < w->height) {
                if (p0_cell_is_valid_start(w, x2, y2) != 0) {
                    best_x = x2;
                    best_y = y2;
                    *cx = best_x;
                    *cy = best_y;
                    return;
                }
            }

            dy += 1;
        }
        r += 1;
    }
}

/* ------------------------------------------------------------------------- */
/* pick_patient_zero (PUBLIC)                                                */
/* Modal picker loop:                                                        */
/*  - Start near visual center and snap to a legal cell.                     */
/*  - Hide the cursor to reduce flicker.                                     */
/*  - Redraw base frame only when needed (first time and on resize/moves).   */
/*  - Draw the overlay brackets often.                                       */
/*  - On Enter/Space, validate and seed; on ESC, abort.                      */
/* Returns 1 on success (seeded), 0 if user cancelled with ESC.              */
/* Patient Zero picker: centers the cursor, snaps to a legal cell,
   draws the base frame + hint + [ ] overlay, and gets ready for input. */
int pick_patient_zero(World* w, Disease* dz)
{
    /* Defensive checks: we must have a world and a disease config */
    if (w == NULL) return 0;
    if (dz == NULL) return 0;

    /* Start near the visual center of the map */
    int cx = w->width / 2;
    int cy = w->height / 2;

    /* Clamp to the last valid cell (protect tiny/odd-sized maps) */
    if (cx >= w->width)  cx = w->width - 1;
    if (cy >= w->height) cy = w->height - 1;

    
    p0_snap_to_nearest(w, &cx, &cy); /* If center is not legal (e.g., sea), jump to the nearest legal Land/Port */
    
    fputs(HIDE_CURSOR, stdout); /* Hide the terminal’s text cursor for a cleaner look during the picker */

    /* --- First paint: draw once, then overlay --- */

    draw_frame_incremental(w);  /* Draw the current world frame (map tiles). Uses the render cache. */

    p0_draw_hint_line(w); /* Print the one-line controls under the map (arrows/Enter/ESC). */

    p0_draw_cursor_brackets(w, cx, cy);  /* Draw the lightweight “[ ]” overlay around the selected cell. */

    fflush(stdout); /* Push everything we just printed to the screen immediately.
                    (stdout is fully buffered elsewhere, so we flush at logical boundaries.) */

    /* Remember where the overlay was last drawn, so we can erase it on movement.
       (We restore those cells from the render cache before drawing the new [ ].) */
    int prev_cx = cx;
    int prev_cy = cy;

	// loop for rezie, keys, movement, confirm, cancel
    while (1) {
        /* Handle zoom/resize: rebuild base, then redraw cursor */
        if (check_console_resize() != 0) {
            p0_handle_resize(w);
            draw_frame_incremental(w);
            p0_draw_hint_line(w);
            p0_draw_cursor_brackets(w, cx, cy);
            fflush(stdout);
        }

        /* Poll key (Windows: non-blocking; POSIX: blocking) */
        int key = read_key_nonblocking();
        if (key == KEY_NONE) {
            sleep_ms(P0_IDLE_SLEEP_MS);
            continue;
        }
        /* Movement: erase old overlay, move, draw new overlay */
        if (key == KEY_LEFT) {
            if (cx > 0) {
                p0_erase_cursor_brackets(w, prev_cx, prev_cy);
                cx -= 1;
                p0_draw_cursor_brackets(w, cx, cy);
                fflush(stdout);
                prev_cx = cx; prev_cy = cy;
            }
            continue;
        }
        if (key == KEY_RIGHT) {
            if (cx + 1 < w->width) {
                p0_erase_cursor_brackets(w, prev_cx, prev_cy);
                cx += 1;
                p0_draw_cursor_brackets(w, cx, cy);
                fflush(stdout);
                prev_cx = cx; prev_cy = cy;
            }
            continue;
        }
        if (key == KEY_UP) {
            if (cy > 0) {
                p0_erase_cursor_brackets(w, prev_cx, prev_cy);
                cy -= 1;
                p0_draw_cursor_brackets(w, cx, cy);
                fflush(stdout);
                prev_cx = cx; prev_cy = cy;
            }
            continue;
        }
        if (key == KEY_DOWN) {
            if (cy + 1 < w->height) {
                p0_erase_cursor_brackets(w, prev_cx, prev_cy);
                cy += 1;
                p0_draw_cursor_brackets(w, cx, cy);
                fflush(stdout);
                prev_cx = cx; prev_cy = cy;
            }
            continue;
        }

        if (key == KEY_ESC) {
            /* Clean up overlay before exiting */
            p0_erase_cursor_brackets(w, prev_cx, prev_cy);
            fputs(SHOW_CURSOR, stdout);
            return 0;
        }

        if (key == KEY_ENTER || key == KEY_SPACE) {
            if (p0_cell_is_valid_start(w, cx, cy) != 0) {
                p0_erase_cursor_brackets(w, prev_cx, prev_cy);
                world_seed_patient_zero(w, dz, cx, cy);
                fputs(SHOW_CURSOR, stdout);
                return 1;
            }
            else {
                int last_sy = 0;
                render_cell_to_screen(w, 0, w->height - 1, NULL, &last_sy);
                int msg_y = last_sy + P0_MSG_OFFSET_ROWS;
                if (msg_y < 1) msg_y = 1;
                cup(msg_y, 2);
                fputs("Not a valid start cell (needs Land or Port with people).", stdout);
                fflush(stdout);
                continue;
            }
        }
        /* ignore other keys */
    }
}

