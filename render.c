// ===================== render.c =====================
// Purpose: Draw the world to the terminal using incremental updates.
// Approach: We cache what was drawn last frame (color + glyph per cell).
//           On each new frame we recalc the "RenderKey" for a cell
//           (infection/death bins + chosen glyph) and only redraw if it changed.
//           This keeps the terminal fast and flicker-free.
//
// IMPORTANT: Behavior is intentionally kept IDENTICAL to the original.
//            Changes below are documentation, structuring, and replacing magic numbers
//            with named defines — not logic changes.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render.h"
#include "ansi.h"
#include "utils.h"
#include "types.h"
#include "hud.h"
#include "disease.h"

// ===================== Tunables & Named Constants =====================
// Colors used for the "cure fade" green tint (post-cure visual mood)
#define CURE_G_R                80
#define CURE_G_G               220
#define CURE_G_B               120

// When we discretize fractions (infected/dead) into bins for the cache key
#define BIN_SCALE_FACTOR      1000
#define BIN_MAX_VALUE         1000

// Base numeric limits for RGB clamping
#define RGB_MIN                  0
#define RGB_MAX                255

// Red overlays for infection/death visual tinting
#define INFECTED_TINT_R        255
#define INFECTED_TINT_G          0
#define INFECTED_TINT_B          0
#define DEAD_TINT_R            100
#define DEAD_TINT_G              0
#define DEAD_TINT_B              0

// Sea "text" color used by the compatibility helper rgb_for_cell()
#define SEA_TEXT_R             180
#define SEA_TEXT_G             220
#define SEA_TEXT_B             255

// Default single-character glyph for sea cells when drawing
#define GLYPH_SEA             ' '

// Where the header/HUD lines live (top two rows)
#define HUD_TITLE_ROW            1
#define HUD_STATS_ROW            2

// Default on-screen placement and spacing for the map
#define MAP_DEFAULT_LEFT         1   // column where cell (0,0) is drawn
#define MAP_DEFAULT_TOP          2   // row    where cell (0,0) is drawn
#define MAP_DEFAULT_STEP_X       1   // columns per cell (1 char each)
#define MAP_DEFAULT_STEP_Y       1   // rows    per cell (one row per cell)

// Weights for how much "redness" (infection/death) drives the cure green tint.
// The green tint alpha = g_cure_fade * (INFECTED_WEIGHT * i_frac + DEAD_WEIGHT * d_frac)
#define CURE_WEIGHT_INFECTED   0.7
#define CURE_WEIGHT_DEAD       0.5

// ANSI convenience (erase to end of line); kept local for clarity
#define ANSI_ERASE_LINE  "\x1b[2K"

// ===================== Module State (persists across frames) =====================
// Last frame's presentation (per cell) so we can skip unchanged cells
static RenderKey* g_prev_frame = NULL;

// 0.0 = no green tint, 1.0 = strong green tint on redder (more infected/dead) cells
static float g_cure_fade = 0.0f;

// Current frame's map layout (published so overlays can translate grid->screen)
static int g_map_origin_x = MAP_DEFAULT_LEFT;
static int g_map_origin_y = MAP_DEFAULT_TOP;
static int g_map_step_x = MAP_DEFAULT_STEP_X;
static int g_map_step_y = MAP_DEFAULT_STEP_Y;

#define CURE_FADE_BUCKETS 16
static int g_cure_bucket = 0;
static int g_prev_cure_bucket = -1;

// ===================== Small Helpers =====================
//
// ansi_fg_rgb / ansi_bg_rgb:
// Send truecolor (24-bit) ANSI escape sequences to set foreground/background.
// We only use foreground in cell drawing; background is kept for potential themes.
//
static void ansi_fg_rgb(int r, int g, int b) { printf("\x1b[38;2;%d;%d;%dm", r, g, b); }
static void ansi_bg_rgb(int r, int g, int b) { printf("\x1b[48;2;%d;%d;%dm", r, g, b); }

//
// lerp_i:
// Linear interpolation between two ints a→b by t in [0..1]. Returns a rounded,
// clamped value in [0..255]. We use it to “blend” one color channel toward another.
//
static int lerp_i(int a, int b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    float v = (1.0f - t) * (float)a + t * (float)b;

    if (v < (float)RGB_MIN) v = (float)RGB_MIN;
    if (v > (float)RGB_MAX) v = (float)RGB_MAX;

    return (int)(v + 0.5f);
}

//
// blend_rgb:
// Move the current RGB color toward a target (tr,tg,tb) by alpha ∈ [0..1].
// This is the core operation behind both infection/death tinting and cure tint.
//
static void blend_rgb(int* r, int* g, int* b, int tr, int tg, int tb, double alpha) {
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;

    *r = lerp_i(*r, tr, (float)alpha);
    *g = lerp_i(*g, tg, (float)alpha);
    *b = lerp_i(*b, tb, (float)alpha);
}

//
// rk_index:
// Convert (x,y) grid coordinates into a 1D index for the frame cache array.
//
static size_t rk_index(const World* w, int x, int y) {
    return (size_t)y * (size_t)w->width + (size_t)x;
}

//
// rk_equal:
// The “is this cell unchanged?” check. If infection/death bins, glyph, and land/sea flag
// are identical to last frame, we skip redrawing the cell.
//
static int rk_equal(RenderKey a, RenderKey b) {
    if (a.i_bin != b.i_bin) return 0;
    if (a.d_bin != b.d_bin) return 0;
    if (a.glyph != b.glyph) return 0;
    if (a.is_sea != b.is_sea) return 0;
    return 1;
}

//
// cell_base_rgb:
// Choose the *base* color from the climate (cold vs hot). Sea is handled specially
// elsewhere, so here we give sea a neutral 0,0,0 base.
//
static void cell_base_rgb(const Cell* c, int* r, int* g, int* b) {
    if (c->terrain == TERRAIN_SEA) {
        *r = 0; *g = 0; *b = 0;
        return;
    }

    if (c->climate == CLIMATE_COLD) {
        *r = COLD_R; *g = COLD_G; *b = COLD_B;   // e.g., CBFCFC
    }
    else {
        *r = HOT_R;  *g = HOT_G;  *b = HOT_B;    // e.g., FCEBCB
    }
}

//
// apply_infection_tint:
// On top of base climate color, add a RED overlay where there's infection (brighter red)
// and a DARK RED overlay where there are deaths. If infection exists but is tiny,
// MIN_INF_TINT ensures a minimal visible hint of red.
//   - w_inf = infected / initial_alive
//   - w_dead = dead / (alive + dead)
// IMPORTANT: This exactly mirrors the original rules.
//
static void apply_infection_tint(const Cell* c, int* R, int* G, int* B) {
    if (c->terrain == TERRAIN_SEA) return;

    double init = (double)c->pop.total + (double)c->pop.dead;
    if (init <= 0.0) {
        // Entirely dead-looking cell
        *R = DEAD_TINT_R; *G = DEAD_TINT_G; *B = DEAD_TINT_B;
        return;
    }

    double w_dead = (double)c->pop.dead / init;
    double w_inf = (double)c->pop.infected / init;
    double w_hea = 1.0 - w_dead - w_inf;
    if (w_hea < 0.0) w_hea = 0.0;

    // Guarantee a small visible red if there's any infection among living population.
    if ((c->pop.infected > 0) && (c->pop.total > 0)) {
        if (w_inf < MIN_INF_TINT) {
            double spare = 1.0 - w_dead;     // we can only “steal” from healthy, not dead
            if (spare < 0.0) spare = 0.0;
            if (MIN_INF_TINT <= spare) {
                w_inf = MIN_INF_TINT;
                w_hea = 1.0 - w_dead - w_inf;
                if (w_hea < 0.0) w_hea = 0.0;
            }
        }
    }

    if (w_inf > 0.0) blend_rgb(R, G, B, INFECTED_TINT_R, INFECTED_TINT_G, INFECTED_TINT_B, w_inf);
    if (w_dead > 0.0) blend_rgb(R, G, B, DEAD_TINT_R, DEAD_TINT_G, DEAD_TINT_B, w_dead);
}

//
// apply_cure_tint:
// After we've applied red/dark-red, optionally blend toward a soft green to signal cure progress.
// The greener the cell, the more “redness” (infection+death) it had, scaled by g_cure_fade.
// Redness is a weighted sum so you can “feel” both infection and death contributions.
//
static void apply_cure_tint(double infected_fraction, double dead_fraction,
    int* R, int* G, int* B) {
    if (g_cure_fade <= 0.0f) return;

    double redness = CURE_WEIGHT_INFECTED * infected_fraction
        + CURE_WEIGHT_DEAD * dead_fraction;

    if (redness < 0.0) redness = 0.0;
    if (redness > 1.0) redness = 1.0;

    double alpha = (double)g_cure_fade * redness;
    if (alpha <= 0.0) return;

    blend_rgb(R, G, B, CURE_G_R, CURE_G_G, CURE_G_B, alpha);
}

// ===================== Public Helpers / API =====================

//
// render_cell_to_screen:
// Translate a map cell coordinate (cx,cy) to a screen coordinate (sx,sy)
// using the *current* published layout.
// NOTE: 'w' is currently unused (layout is global), but kept for future flexibility.
//
void render_cell_to_screen(const World* w, int cx, int cy, int* sx, int* sy) {
    (void)w;

    if (sx != NULL) {
        *sx = g_map_origin_x + cx * g_map_step_x;
    }
    if (sy != NULL) {
        *sy = g_map_origin_y + cy * g_map_step_y;
    }
}

//
// alloc_frame_cache / free_frame_cache / invalidate_frame_cache:
// Manage the 1D array of RenderKey used to remember last frame's drawn state.
// - alloc_frame_cache: allocate and zero-initialize
// - free_frame_cache:  release and reset pointer
// - invalidate_frame_cache: fill with 0xFF so every cell “appears changed” next frame
//
void alloc_frame_cache(const World* w) {
    size_t n = (size_t)w->width * (size_t)w->height;
    g_prev_frame = (RenderKey*)calloc(n, sizeof(RenderKey));
}

void free_frame_cache(void) {
    free(g_prev_frame);
    g_prev_frame = NULL;
}

void invalidate_frame_cache(const World* w) {
    if (g_prev_frame == NULL) return;

    size_t n = (size_t)w->width * (size_t)w->height;
    memset(g_prev_frame, 0xFF, n * sizeof(RenderKey));
}

//
// render_set_cure_fade:
// External knob for the cure tint strength in [0..1]. Safe to call per tick.
//
void render_set_cure_fade(float t) {
    /* Clamp input to [0..1] */
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }

    /* Store the exact float for color math */
    g_cure_fade = t;

    /* Quantize to buckets so we only repaint on noticeable steps */
    int b = (int)(t * (float)(CURE_FADE_BUCKETS - 1) + 0.5f);
    if (b < 0) {
        b = 0;
    }
    if (b >= CURE_FADE_BUCKETS) {
        b = CURE_FADE_BUCKETS - 1;
    }
    g_cure_bucket = b;
}



//
// draw_header_and_hud:
// Paint a 2-line header at the top with a title and a compact world stats HUD.
// The HUD numbers are computed from the World via compute_world_stats().
//
void draw_header_and_hud(const World* w, int tick, const Disease* dz) {
    (void)dz; // not used here; HUD is global summary

    HudStats s;
    compute_world_stats(w, &s);

    // Optional: disable wrap (macro likely defined in ansi.h)
    fputs(WRAP_OFF, stdout);

    // Row 1: Title
    cup(HUD_TITLE_ROW, 1);
    fputs(ANSI_ERASE_LINE, stdout);
    printf("Plague Simulation (W=%d H=%d)%s", w->width, w->height, ANSI_RESET);

    // Row 2: HUD stats
    cup(HUD_STATS_ROW, 1);
    fputs(ANSI_ERASE_LINE, stdout);
    printf("Alive: %llu (%.1f%%)  Infected: %llu (%.1f%%)  Dead: %llu (%.1f%%)   Tick: %d%s",
        s.alive_pop, s.pct_alive,
        s.infected_pop, s.pct_infected,
        s.dead_pop, s.pct_dead,
        tick,
        ANSI_RESET);
}

// ===================== Incremental Renderer =====================
//
// draw_frame_incremental:
// For each cell, compute its new RenderKey (bins + glyph + sea flag).
// If it differs from the cached key, we recompute the final color (base climate,
// infection overlay, cure overlay), draw the glyph, and update both the cell's
// “display” fields and the cache entry.
//
// Complex bits explained:
//   • Fraction binning: we multiply fractions by BIN_SCALE_FACTOR and round, so tiny
//     changes don’t constantly invalidate the cache due to float noise.
//   • Glyph choice: we normalize sea to a space; land cells use the presentation glyph
//     (c->disp_glyph) for drawing, but the *input* “now” key is built from c->raw.
//     The cache gets the glyph we actually drew.
//
void draw_frame_incremental(const World* w) {
#ifdef WRAP_OFF
    /* Prevent line-wrapping artifacts after console zoom/resize */
    fputs(WRAP_OFF, stdout);
#endif

    /* Per-frame layout (if you later center the map, change these 4 and overlays will still work) */
    const int MAP_LEFT = MAP_DEFAULT_LEFT;
    const int MAP_TOP = MAP_DEFAULT_TOP;
    const int STEP_X = MAP_DEFAULT_STEP_X;
    const int STEP_Y = MAP_DEFAULT_STEP_Y;

    /* Publish the layout for overlays (patient-zero picker, markers, etc.) */
    if (MAP_LEFT > 0) {
        g_map_origin_x = MAP_LEFT;
    }
    else {
        g_map_origin_x = MAP_DEFAULT_LEFT;
    }

    if (MAP_TOP > 0) {
        g_map_origin_y = MAP_TOP;
    }
    else {
        g_map_origin_y = MAP_DEFAULT_TOP;
    }

    if (STEP_X > 0) {
        g_map_step_x = STEP_X;
    }
    else {
        g_map_step_x = MAP_DEFAULT_STEP_X;
    }

    if (STEP_Y > 0) {
        g_map_step_y = STEP_Y;
    }
    else {
        g_map_step_y = MAP_DEFAULT_STEP_Y;
    }

    for (int y = 0; y < w->height; ++y) {
        for (int x = 0; x < w->width; ++x) {
            const Cell* c = &w->grid[y][x];

            /* Fractions that drive tinting and binning:
               - i_frac = infected among the living
               - d_frac = dead among all (alive + dead) */
            double i_frac = 0.0;
            double d_frac = 0.0;

            int alive = c->pop.total;
            int dead = c->pop.dead;

            if ((alive + dead) > 0) {
                if (alive > 0) {
                    i_frac = (double)c->pop.infected / (double)alive;
                }
                d_frac = (double)dead / (double)(alive + dead);
            }

            /* Convert fractions into integer bins to make equality checks stable */
            int i_bin = (int)(i_frac * (double)BIN_SCALE_FACTOR + 0.5);
            int d_bin = (int)(d_frac * (double)BIN_SCALE_FACTOR + 0.5);
            if (i_bin > BIN_MAX_VALUE) i_bin = BIN_MAX_VALUE;
            if (d_bin > BIN_MAX_VALUE) d_bin = BIN_MAX_VALUE;

            /* Build the "current" key from raw glyph and sea/land */
            char glyph_now;
            if (c->terrain == TERRAIN_SEA) {
                glyph_now = GLYPH_SEA;
            }
            else {
                glyph_now = c->raw;
            }

            RenderKey now;
            now.i_bin = (unsigned short)i_bin;
            now.d_bin = (unsigned short)d_bin;
            now.glyph = (unsigned char)glyph_now;
            now.is_sea = (unsigned char)((c->terrain == TERRAIN_SEA) ? 1 : 0);

            size_t idx = rk_index(w, x, y);
            RenderKey prev = g_prev_frame[idx];

            /* NEW: if cure fade bucket changed, selectively repaint only “red” cells.
               Cells with any infection/death (prev bins > 0) change color as fade progresses,
               even if their RenderKey did not change. */
            int cure_repaint_needed = 0;
            if (g_cure_bucket != g_prev_cure_bucket) {
                if (prev.i_bin > 0 || prev.d_bin > 0) {
                    cure_repaint_needed = 1;
                }
            }

            /* Redraw only if something meaningful changed OR during cure-bucket repaint */
            if (!rk_equal(now, prev) || cure_repaint_needed != 0) {
                /* Start at base climate color */
                int R, G, B;
                cell_base_rgb(c, &R, &G, &B);

                /* Layer infection/death red overlays */
                apply_infection_tint(c, &R, &G, &B);

                /* Optionally layer cure green tint on top (driven by "redness") */
                apply_cure_tint(i_frac, d_frac, &R, &G, &B);

                /* Choose the actual glyph to draw: sea is a space; land uses display glyph */
                char out_glyph;
                if (c->terrain == TERRAIN_SEA) {
                    out_glyph = GLYPH_SEA;
                }
                else {
                    out_glyph = c->disp_glyph;
                }

                /* Build the key we are actually going to store in the cache (uses the drawn glyph) */
                RenderKey out;
                out.i_bin = (unsigned short)i_bin;
                out.d_bin = (unsigned short)d_bin;
                out.glyph = (unsigned char)out_glyph;
                out.is_sea = (unsigned char)((c->terrain == TERRAIN_SEA) ? 1 : 0);

                /* Draw the glyph at its screen position using the published layout */
                int screen_x = g_map_origin_x + x * g_map_step_x;
                int screen_y = g_map_origin_y + y * g_map_step_y;
                cup(screen_y, screen_x);
                ansi_fg_rgb(R, G, B);
                fputc(out_glyph, stdout);
                fputs(ANSI_RESET, stdout);

                /* Update per-cell presentation fields and cache (cache only if key changed) */
                ((Cell*)c)->disp_i_bin = i_bin;
                ((Cell*)c)->disp_d_bin = d_bin;
                ((Cell*)c)->disp_glyph = out_glyph;

                if (!rk_equal(now, prev)) {
                    g_prev_frame[idx] = out;
                }
            }
        }
    }

    /* NEW: mark the cure-bucket change handled for this frame */
    if (g_prev_cure_bucket != g_cure_bucket) {
        g_prev_cure_bucket = g_cure_bucket;
    }
}

// ===================== Cache-Based Single-Cell Restore =====================
//
// restore_cell_from_cache:
// Re-paint a *single* cell using only what we cached last frame.
// Used by overlays (e.g., moving ships) to erase themselves cleanly.
//
// NOTE (intentional quirk preserved):
// This uses fixed coordinates cup(2 + y, 1 + x) instead of the published layout.
// That’s how the original behaved, so we keep it identical to avoid subtle changes
// when the layout changes. If you ever re-layout the map, remember this function.
//
void restore_cell_from_cache(const World* w, int x, int y) {
    size_t idx = rk_index(w, x, y);
    RenderKey prev = g_prev_frame[idx];
    const Cell* c = &w->grid[y][x];

    // Start from base climate color
    int R, G, B;
    cell_base_rgb(c, &R, &G, &B);

    // Reconstruct an *approximate* population split from the cached bins.
    // We create a local proxy cell with population consistent with the bins,
    // then reuse apply_infection_tint/apply_cure_tint to regenerate the same color.
    Cell proxy = *c;
    unsigned long long init = (unsigned long long)c->pop.total
        + (unsigned long long)c->pop.dead;

    if (init == 0ULL) {
        R = DEAD_TINT_R; G = DEAD_TINT_G; B = DEAD_TINT_B;
    }
    else {
        double i_frac = (double)prev.i_bin / (double)BIN_SCALE_FACTOR;
        double d_frac = (double)prev.d_bin / (double)BIN_SCALE_FACTOR;

        proxy.pop.dead = (unsigned long long)(d_frac * (double)init + 0.5);
        if (proxy.pop.dead > init) {
            proxy.pop.dead = init;
        }

        proxy.pop.infected = (unsigned long long)(i_frac * (double)init + 0.5);
        if (proxy.pop.infected + proxy.pop.dead > init) {
            proxy.pop.infected = init - proxy.pop.dead;
        }

        proxy.pop.total = (int)(init - proxy.pop.dead);

        apply_infection_tint(&proxy, &R, &G, &B);
        apply_cure_tint(i_frac, d_frac, &R, &G, &B);
    }

    // NOTE: fixed coordinates preserved from original code:
    cup(MAP_DEFAULT_TOP + y, MAP_DEFAULT_LEFT + x);
    ansi_fg_rgb(R, G, B);
    fputc((char)prev.glyph, stdout);
    fputs(ANSI_RESET, stdout);
}

// ===================== Optional Compatibility Helper =====================
//
// rgb_for_cell (static):
// Legacy helper kept for any callers that directly ask for a color (without caching).
// It computes a straight weighted blend between “healthy white”, “infected red”,
// and “dead dark red”, then applies cure tint similarly to the main path.
//
static void rgb_for_cell(const Cell* c, int* R, int* G, int* B) {
    if (c->terrain == TERRAIN_SEA) {
        *R = SEA_TEXT_R; *G = SEA_TEXT_G; *B = SEA_TEXT_B;
        return;
    }

    double init = (double)c->pop.total + (double)c->pop.dead;
    if (init <= 0.0) {
        *R = DEAD_TINT_R; *G = DEAD_TINT_G; *B = DEAD_TINT_B;
        return;
    }

    double w_dead = (double)c->pop.dead / init;
    double w_inf = (double)c->pop.infected / init;
    double w_hea = 1.0 - w_dead - w_inf;
    if (w_hea < 0.0) w_hea = 0.0;

    // Ensure minimal red if some infection exists among living
    if ((c->pop.infected > 0) && (c->pop.total > 0)) {
        if (w_inf < MIN_INF_TINT) {
            w_inf = MIN_INF_TINT;
            w_hea = 1.0 - w_dead - w_inf;
            if (w_hea < 0.0) {
                w_inf = 1.0 - w_dead;
                if (w_inf < 0.0) w_inf = 0.0;
                w_hea = 0.0;
            }
        }
    }

    // Target colors for the weighted average
    const int HEALTHY_R = 255, HEALTHY_G = 255, HEALTHY_B = 255;
    const int INF_R = INFECTED_TINT_R, INF_G = INFECTED_TINT_G, INF_B = INFECTED_TINT_B;
    const int DEAD_R = DEAD_TINT_R, DEAD_G = DEAD_TINT_G, DEAD_B = DEAD_TINT_B;

    // Weighted average across healthy/infected/dead “layers”
    double rf = w_hea * HEALTHY_R + w_inf * INF_R + w_dead * DEAD_R;
    double gf = w_hea * HEALTHY_G + w_inf * INF_G + w_dead * DEAD_G;
    double bf = w_hea * HEALTHY_B + w_inf * INF_B + w_dead * DEAD_B;

    int r = (int)(rf + 0.5); if (r < RGB_MIN) r = RGB_MIN; if (r > RGB_MAX) r = RGB_MAX;
    int g = (int)(gf + 0.5); if (g < RGB_MIN) g = RGB_MIN; if (g > RGB_MAX) g = RGB_MAX;
    int b = (int)(bf + 0.5); if (b < RGB_MIN) b = RGB_MIN; if (b > RGB_MAX) b = RGB_MAX;

    *R = r; *G = g; *B = b;

    // Apply cure tint here too if someone uses this helper directly.
    {
        double denom = (double)c->pop.total + (double)c->pop.dead;
        double i_frac = 0.0;
        double d_frac = 0.0;

        if (c->pop.total > 0)  i_frac = (double)c->pop.infected / (double)c->pop.total;
        if (denom > 0.0)       d_frac = (double)c->pop.dead / denom;

        apply_cure_tint(i_frac, d_frac, R, G, B);
    }
}
