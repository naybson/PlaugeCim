// ===================== end_grapth.c =====================
// Purpose: Render the stacked-bar timeline of turning points at the end screen.
//
// What you’ll see on screen:
//   - Each bar = one TurnPoint (tick snapshot).
//   - Height is split into: grey(dead) + red(infected) + blue(alive).
//   - Once cure is active at that tick, infected merges into blue (policy: “treated”).
//
// Implementation notes (unchanged behavior):
//   - Bars are streamed left→right; if there are too many points to fit, we stride
//     (skip evenly) but keep the last one visible.
//   - We expose an optional "hitmap" so the interactive end screen can map a
//     bar column back to the exact TP node you selected.
//   - No ‘?:’ ternaries; simple, explicit branching for readability.

#include "end_graph.h"   /* (typo kept intentionally to match existing includes) */
#include "ansi.h"
#include <stdio.h>
#include <stdint.h>       /* uint64_t */

/* ============================== Color & Glyph Knobs ============================== */
/* These are terminal RGB foregrounds for segments. Tweak if you want a different palette. */
#define COL_BLUE_R    70
#define COL_BLUE_G   170
#define COL_BLUE_B   240

#define COL_RED_R    220
#define COL_RED_G     20
#define COL_RED_B     20

#define COL_GREY_R   130
#define COL_GREY_G   130
#define COL_GREY_B   130

/* Bar fill glyph (ASCII-safe). */
#define BAR_CHAR     '#'

/* ============================== Small ANSI Helpers =============================== */
static void fg_rgb(int r, int g, int b) {
    /* Clamp to [0,255] to avoid weird escape codes if someone tweaks constants. */
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    printf("\x1b[38;2;%d;%d;%dm", r, g, b);
}
static void set_blue(void) { fg_rgb(COL_BLUE_R, COL_BLUE_G, COL_BLUE_B); }
static void set_red(void) { fg_rgb(COL_RED_R, COL_RED_G, COL_RED_B); }
static void set_grey(void) { fg_rgb(COL_GREY_R, COL_GREY_G, COL_GREY_B); }

/* ============================== Drawing Primitives =============================== */
/* Draw a vertical stack segment starting at base_y and going "rows" upward. */
static void draw_stack_segment(int col_x, int base_y, int rows) {
    if (rows <= 0) {
        return;
    }
    for (int i = 0; i < rows; ++i) {
        int y = base_y - i;  /* go up one row per glyph */
        cup(y, col_x);
        fputc(BAR_CHAR, stdout);
    }
}

/* Convert counts to stacked rows; sum equals ENDGRAPH_BAR_H.
   - We scale each component by ENDGRAPH_BAR_H / total with rounding.
   - If cure is active at that tick, we *add* red (infected) rows into blue (alive)
     and set red rows to zero — same visual policy as before.
   - Rounding remainder is dumped into blue so total rows always match the bar height. */
static void rows_from_point(const TurnPoint* p, int* out_dead, int* out_inf, int* out_blue) {
    if (out_dead == NULL || out_inf == NULL || out_blue == NULL) {
        return;
    }

    int r_dead = 0;
    int r_inf = 0;
    int r_blue = 0;

    if (p == NULL) {
        *out_dead = 0; *out_inf = 0; *out_blue = 0;
        return;
    }
    if (p->total == 0ULL) {
        *out_dead = 0; *out_inf = 0; *out_blue = 0;
        return;
    }

    /* Scale each slice to bar rows with rounding. */
    r_dead = (int)((double)p->dead * (double)ENDGRAPH_BAR_H / (double)p->total + 0.5);
    r_inf = (int)((double)p->infected * (double)ENDGRAPH_BAR_H / (double)p->total + 0.5);
    r_blue = (int)((double)p->alive * (double)ENDGRAPH_BAR_H / (double)p->total + 0.5);

    /* Cure policy: infected merges into blue. */
    if (p->cure_active != 0) {
        r_blue += r_inf;
        r_inf = 0;
    }

    /* Rounding fix: ensure r_dead + r_inf + r_blue == ENDGRAPH_BAR_H.
       We push any remainder into blue (the most visually neutral segment). */
    {
        int used = r_dead + r_inf + r_blue;
        int delta = ENDGRAPH_BAR_H - used;
        if (delta != 0) {
            r_blue += delta;
            if (r_blue < 0) {
                r_blue = 0;  /* safety clamp; practically unreachable */
            }
        }
    }

    *out_dead = r_dead;
    *out_inf = r_inf;
    *out_blue = r_blue;
}

/* Draw one bar at a specific column and baseline. */
static void draw_bar_at(int col_x, int base_y, const TurnPoint* p) {
    int rows_dead = 0, rows_inf = 0, rows_blue = 0;
    rows_from_point(p, &rows_dead, &rows_inf, &rows_blue);

    set_grey(); draw_stack_segment(col_x, base_y, rows_dead);
    if (rows_inf > 0) { set_red();  draw_stack_segment(col_x, base_y - rows_dead, rows_inf); }
    if (rows_blue > 0) { set_blue(); draw_stack_segment(col_x, base_y - rows_dead - rows_inf, rows_blue); }

    fputs(ANSI_RESET, stdout);
}

/* ============================== Stream & Hitmap ================================= */
/* Stream the list and draw bars left→right. If there are too many points to fit,
   we compute a stride (ceil division) and sample evenly so we still cover
   the whole timeline without squishing bars. Optionally fill a hitmap. */
static void draw_streamed(const TurnTracker* tp, EndGraphHitmap* hm) {
    if (tp == NULL) {
        return;
    }
    if (tp->count == 0) {
        return;
    }

    /* How many bars can we fit horizontally? */
    int per_bar = (ENDGRAPH_COL_W + ENDGRAPH_GAP);
    int avail_cols = (ENDGRAPH_SCREEN_COLS - ENDGRAPH_LEFT_X);
    if (per_bar <= 0 || avail_cols <= 0) {
        return;
    }
    int max_cols = avail_cols / per_bar;
    if (max_cols <= 0) {
        return;
    }

    /* Compute stride = ceil(tp->count / max_cols). This keeps the end covered. */
    int stride = 1;
    if ((int)tp->count > max_cols) {
        int num = (int)tp->count + max_cols - 1; /* ceil division trick */
        stride = num / max_cols;
        if (stride < 1) {
            stride = 1;
        }
    }

    const TPNode* it = tp->head;
    int i = 0;          /* index in the full list */
    int drawn = 0;      /* index among drawn bars */

    while (it != NULL) {
        /* Sample every 'stride' nodes: 0, stride, 2*stride, ... */
        if ((i % stride) == 0) {
            int col_x = ENDGRAPH_LEFT_X + drawn * per_bar;
            draw_bar_at(col_x, ENDGRAPH_BASE_Y, &it->p);

            if (hm != NULL && drawn < ENDGRAPH_MAX_BARS) {
                hm->node[drawn] = it;
                hm->col_x[drawn] = col_x;
            }
            drawn += 1;
        }

        it = it->next;
        i += 1;
    }

    if (hm != NULL) {
        hm->count = drawn;
        if (hm->count > ENDGRAPH_MAX_BARS) {
            hm->count = ENDGRAPH_MAX_BARS; /* safety clamp */
        }
    }

    fflush(stdout);
}

/* ============================== Public Wrappers ================================= */
void endgraph_show(const TurnTracker* tp) {
    draw_streamed(tp, NULL);
}

void endgraph_show_with_hitmap(const TurnTracker* tp, EndGraphHitmap* hm) {
    if (hm != NULL) {
        hm->count = 0; /* initialize in case draw aborts early */
    }
    draw_streamed(tp, hm);
}
