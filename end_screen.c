// ===================== end_screen.c =====================
// Purpose: Draw the final "end of simulation" screens.
//
// Two modes:
//   1) end_screen_show            -> static summary (ESC/Enter to exit)
//   2) end_screen_show_interactive-> interactive: move a selector across bars,
//      and show details (tick + alive/infected/dead %) for the selected turning point.
//
// Plus: Press 'S' to save current disease preset (asks for a name).
//
// Notes on design:
//   - All drawing is done with simple ASCII/ANSI so it works in a dumb terminal.
//   - We keep logic identical to your original; this is a readability pass.
//   - Small additions for saving use a simple blocking name prompt.
//   - We replace magic numbers with #defines and explain non-obvious bits.
//
// Dependencies you provide:
//   - ansi.h            : cursor movement + color escape helpers/macros
//   - end_condition.h   : endreason_title(EndReason)
//   - end_graph.h      : endgraph_show* + layout constants
//   - turnpoints.h      : TurnTracker / TurnPoint structures (uint64_t counts)
//   - disease_io.h      : DISEASES_FILE_PATH, DISEASE_NAME_MAX, disease_save_append(...)
//   - disease.h / config.h: Disease / SimConfig

#include <stdio.h>
#include <string.h>
#include <stdint.h>        /* uint64_t */

#ifdef _WIN32
#include <conio.h>       /* _getch on Windows */
#endif

#include "ansi.h"
#include "end_screen.h"
#include "end_condition.h"  /* endreason_title(...) */
#include "turnpoints.h"
#include "end_graph.h"     /* endgraph_show*, ENDGRAPH_BASE_Y, ENDGRAPH_SCREEN_COLS */
#include "disease_io.h"   /* DISEASES_FILE_PATH + save API */
#include "config.h"       /* SimConfig */
#include "disease.h"      /* Disease   */

/* Recorder filled during the run (provided elsewhere). */
extern TurnTracker g_tp;

/* ============================== Save Context ==================================== */
/* Set once by the caller so the end screen can save the "current disease". */
static const Disease* g_es_dz = NULL;
static const SimConfig* g_es_cfg = NULL;

void end_screen_set_save_context(const Disease* dz, const SimConfig* cfg) {
    g_es_dz = dz;
    g_es_cfg = cfg;
}

/* ============================== Constants & Defines ============================== */

/* Default terminal width used when we "center" lines without querying the real console. */
#define SCREEN_COLS_DEFAULT        100

/* Selector (the little 'v' under the selected bar) color and glyph. */
#define SELECTOR_RGB_R             230
#define SELECTOR_RGB_G             140
#define SELECTOR_RGB_B              40
#define SELECTOR_GLYPH             'v'

/* Panel layout parameters (info box drawn under the selector row). */
#define PANEL_MARGIN_X               4                 /* left/right padding from chart edges */
#define PANEL_WIDTH_PAD              8                 /* shrink width by this much from full chart width */
#define PANEL_TOP_OFFSET_FROM_BASE   3                 /* lines below the chart base (BASE_Y) */
#define PANEL_HEIGHT                 8                 /* lines tall */

/* Selector row is drawn one line under the bars. */
#define SELECTOR_ROW_OFFSET          1

/* Minimal dimensions for drawing a simple ASCII box. */
#define BOX_MIN_W                    2
#define BOX_MIN_H                    2

/* ============================== Tiny Types & Helpers ============================= */

/* Simple rectangle for drawing panels. */
typedef struct { int x, y, w, h; } Rect;

/* Center a line horizontally at a given terminal row. */
static void center_line_at_row(int row, const char* s, int total_cols) {
    if (s == NULL) return;
    if (total_cols < 1) total_cols = SCREEN_COLS_DEFAULT;

    int len = (int)strlen(s);
    int col = (total_cols - len) / 2;
    if (col < 1) col = 1;

    cup(row, col);
    fputs(s, stdout);
}

/* Clear a rectangular region by printing spaces. */
static void clear_rect(Rect r) {
    if (r.w <= 0 || r.h <= 0) return;

    for (int y = r.y; y < r.y + r.h; ++y) {
        cup(y, r.x);
        for (int i = 0; i < r.w; ++i) {
            fputc(' ', stdout);
        }
    }
}

/* Draw a simple ASCII box (----, ||, + corners). */
static void box(Rect r) {
    if (r.w < BOX_MIN_W || r.h < BOX_MIN_H) return;

    int x0 = r.x, y0 = r.y;
    int x1 = r.x + r.w - 1;
    int y1 = r.y + r.h - 1;

    /* top + bottom borders */
    for (int x = x0; x <= x1; ++x) { cup(y0, x); fputc('-', stdout); }
    for (int x = x0; x <= x1; ++x) { cup(y1, x); fputc('-', stdout); }

    /* left + right borders */
    for (int y = y0; y <= y1; ++y) { cup(y, x0); fputc('|', stdout); }
    for (int y = y0; y <= y1; ++y) { cup(y, x1); fputc('|', stdout); }

    /* corners last so they overwrite border chars at the ends */
    cup(y0, x0); fputc('+', stdout);
    cup(y0, x1); fputc('+', stdout);
    cup(y1, x0); fputc('+', stdout);
    cup(y1, x1); fputc('+', stdout);
}

/* Percent with rounding to nearest integer, clamped to [0,100]. */
static int pct_rounded(uint64_t part, uint64_t total) {
    if (total == 0ULL) return 0;
    double p = ((double)part * 100.0) / (double)total;
    int r = (int)(p + 0.5);
    if (r < 0) r = 0;
    if (r > 100) r = 100;
    return r;
}

/* Build a short human string for the reason of a turning point. */
static void tp_reason_text(const TurnPoint* tp, char* out, size_t n) {
    if (out == NULL || n == 0) return;

    if (tp == NULL) { (void)snprintf(out, n, "—"); return; }

    if (tp->reason == TP_SYMPTOM) { (void)snprintf(out, n, "Symptom added - #%d", (int)tp->detail); return; }
    if (tp->reason == TP_FIRST_INFECTED_SHIP) { (void)snprintf(out, n, "First infected ship appears"); return; }
    if (tp->reason == TP_INF_THRESHOLD) { (void)snprintf(out, n, "Infected >= %d%%", (int)tp->detail); return; }
    if (tp->reason == TP_DEAD_THRESHOLD) { (void)snprintf(out, n, "Dead >= %d%%", (int)tp->detail); return; }
    if (tp->reason == TP_CURE_ACTIVE) { (void)snprintf(out, n, "Cure becomes active"); return; }
    if (tp->reason == TP_FINAL_TICK) { (void)snprintf(out, n, "Final snapshot"); return; }

    (void)snprintf(out, n, "Turning point");
}

/* Simple safe trim (in place). Returns pointer to start (within the same buffer). */
static char* trim_inplace(char* s) {
    if (!s) return s;
    /* rtrim */
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) { s[--n] = '\0'; }
    /* ltrim */
    while (*s == ' ' || *s == '\t') ++s;
    return s;
}

/* Ask for a preset name and save current disease+cfg (if context exists). */
/* Ask for a preset name and save current disease+cfg (with overwrite/rename check). */
static void prompt_and_save_current_preset(void) {
    if (g_es_dz == NULL || g_es_cfg == NULL) {
        return; /* no context provided by main() */
    }
    char buf[DISEASE_NAME_MAX] = { 0 };
    int len = 0;

    fputs("\n\nEnter preset name (A-Z, a-z, 0-9, _ or -). Enter to save, ESC to cancel.\n", stdout);
    for (;;) {
        printf("Name: %s_\r", buf);
        fflush(stdout);

        int ch = _getch();
        if (ch == 27) { /* ESC */
            fputs("\nCanceled.\n", stdout);
            return;
        }
        if (ch == '\r' || ch == '\n') {
            if (len > 0) {
                /* IMPORTANT: use the same protected save you use in setup.c */
                int ok = disease_save_with_prompt(DISEASES_FILE_PATH, buf, g_es_dz, g_es_cfg);
                if (ok) {
                    printf("\nSaved as [%s] to %s.\n", buf, DISEASES_FILE_PATH);
                }
                else {
                    fputs("\nSave aborted.\n", stdout);
                }
            }
            else {
                fputs("\nCanceled (empty name).\n", stdout);
            }
            return;
        }
        if (ch == 8 /* backspace */) {
            if (len > 0) buf[--len] = '\0';
            continue;
        }

        int ok = 0;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) ok = 1;
        if (ch >= '0' && ch <= '9') ok = 1;
        if (ch == '_' || ch == '-') ok = 1;
        if (ok && len < (DISEASE_NAME_MAX - 1)) {
            buf[len++] = (char)ch;
            buf[len] = '\0';
        }
    }
}

/* ============================== Info Panel ====================================== */

// Draws the info panel under the end-of-simulation graph,
// showing turning point details like reason, tick, and percentages.
static void render_info_panel(Rect panel, const TurnPoint* sel) {
    clear_rect(panel);   // Clear the panel area (wipe old text)
    box(panel);          // Draw an ASCII box around the panel

    // If the panel is too small to show useful info, exit early
    if (panel.w < 20 || panel.h < 6) {
        return;
    }

    // Calculate the middle x-coordinate of the panel (for centering text)
    int mid = panel.x + panel.w / 2;

    // Create the first line that describes the reason for this turning point
    char reason_line[128];
    tp_reason_text(sel, reason_line, sizeof(reason_line));

    // ---- TITLE: "Turning point: [reason]" ----
    {
        char title[160];
        snprintf(title, sizeof(title), "Turning point: %s", reason_line);
        int col = mid - (int)strlen(title) / 2;
        if (col < 1) col = 1;
        cup(panel.y + 1, col);       // Move cursor to title row
        fputs(title, stdout);        // Print the title
    }

    // ---- STATISTICS: Tick + Alive / Infected / Dead ----
    if (sel != NULL) {
        char line[96];

        // Show which tick this point happened at
        snprintf(line, sizeof(line), "Tick: %d", sel->tick);
        int col = mid - (int)strlen(line) / 2;
        if (col < 1) col = 1;
        cup(panel.y + 3, col);
        fputs(line, stdout);

        // Convert raw counts to percentages
        int a = pct_rounded(sel->alive, sel->total);
        int i = pct_rounded(sel->infected, sel->total);
        int d = pct_rounded(sel->dead, sel->total);

        // Print: Alive %
        snprintf(line, sizeof(line), "Alive: %d%%", a);
        col = mid - (int)strlen(line) / 2;
        if (col < 1) col = 1;
        cup(panel.y + 4, col);
        fputs(line, stdout);

        // Print: Infected %
        snprintf(line, sizeof(line), "Infected: %d%%", i);
        col = mid - (int)strlen(line) / 2;
        if (col < 1) col = 1;
        cup(panel.y + 5, col);
        fputs(line, stdout);

        // Print: Dead %
        snprintf(line, sizeof(line), "Dead: %d%%", d);
        col = mid - (int)strlen(line) / 2;
        if (col < 1) col = 1;
        cup(panel.y + 6, col);
        fputs(line, stdout);
    }
}

/* Wipe the selector row (the line directly under the graph bars). */
static void clear_selector_row(void) {
    int y = ENDGRAPH_BASE_Y + SELECTOR_ROW_OFFSET;
    cup(y, 1);
    for (int i = 0; i < ENDGRAPH_SCREEN_COLS; ++i) {
        fputc(' ', stdout);
    }
}

/* Draw the selector marker ('v') under the given screen column. */
static void draw_selector(int col_x) {
    if (col_x < 1) return;
    int y = ENDGRAPH_BASE_Y + SELECTOR_ROW_OFFSET;
    cup(y, col_x);
    printf("\x1b[38;2;%d;%d;%dm", SELECTOR_RGB_R, SELECTOR_RGB_G, SELECTOR_RGB_B);
    fputc(SELECTOR_GLYPH, stdout);
    fputs(ANSI_RESET, stdout);
}

/* ============================== Key Handling ==================================== */
/* We use blocking reads for simplicity; these are small end-of-run screens. */
enum { KEY_NONE = 0, KEY_LEFT = 1, KEY_RIGHT = 2, KEY_ESC = 3, KEY_ENTER = 4, KEY_SAVE = 5 };

static int read_key_blocking(void) {
    /* Windows: _getch returns either a char or a prefix (0/224) + code. */
    int ch = _getch();
    if (ch == 27)                 return KEY_ESC;
    if (ch == '\r' || ch == '\n') return KEY_ENTER;
    if (ch == 's' || ch == 'S')   return KEY_SAVE;
    if (ch == 0 || ch == 224) {
        int ch2 = _getch();
        if (ch2 == 75) return KEY_LEFT;    /* left arrow */
        if (ch2 == 77) return KEY_RIGHT;   /* right arrow */
    }
    return KEY_NONE;
}


/* ============================== Public Screens ================================== */

/* Static end screen: show title, reason, tick, and the final chart.
   User exits with ESC or Enter. Press S to save current disease preset. */
void end_screen_show(EndReason why, int tick) {
    fputs("\x1b[2J\x1b[H", stdout);  /* clear screen + home */

    center_line_at_row(3, "Plague Simulation", SCREEN_COLS_DEFAULT);
    center_line_at_row(5, endreason_title(why), SCREEN_COLS_DEFAULT);

    {
        char buf[64];
        (void)snprintf(buf, sizeof(buf), "Ended at tick %d", tick);
        center_line_at_row(7, buf, SCREEN_COLS_DEFAULT);
    }

    endgraph_show(&g_tp);

    if (g_es_dz && g_es_cfg) {
        center_line_at_row(ENDGRAPH_BASE_Y + 2,
            "Press S to save preset  *  ESC/Enter to exit",
            SCREEN_COLS_DEFAULT);
    }
    else {
        center_line_at_row(ENDGRAPH_BASE_Y + 2,
            "Press ESC or Enter to exit",
            SCREEN_COLS_DEFAULT);
    }
    fflush(stdout);

    for (;;) {
        int k = read_key_blocking();
        if (k == KEY_SAVE) {
            system("cls");
            if (g_es_dz && g_es_cfg) 
            {
                prompt_and_save_current_preset();
                /* Reprint the tip line so UI looks clean after the prompt. */
                if (g_es_dz && g_es_cfg) {
                    center_line_at_row(ENDGRAPH_BASE_Y + 2,
                        "Press S to save preset  *  ESC/Enter to exit",
                        SCREEN_COLS_DEFAULT);
                }
                fflush(stdout);
            }
            continue;
        }
        if (k == KEY_ENTER || k == KEY_ESC) {
            break;
        }
    }
}

/* Interactive inspector: same as above, plus a selector you can move with
   left/right arrows. The info panel shows the data for the selected bar. */
void end_screen_show_interactive(EndReason why, int tick) {
    fputs("\x1b[2J\x1b[H", stdout);  /* clear screen + home */

    center_line_at_row(3, "Plague Simulation", SCREEN_COLS_DEFAULT);
    center_line_at_row(5, endreason_title(why), SCREEN_COLS_DEFAULT);

    {
        char buf[64];
        (void)snprintf(buf, sizeof(buf), "Simulation ends at tick %d", tick);
        center_line_at_row(7, buf, SCREEN_COLS_DEFAULT);
    }

    /* Draw the graph and collect "hitmap": per-bar column and node pointers. */
    EndGraphHitmap hm;
    hm.count = 0;
    endgraph_show_with_hitmap(&g_tp, &hm);

    if (hm.count <= 0) {
        if (g_es_dz && g_es_cfg) {
            center_line_at_row(ENDGRAPH_BASE_Y + 2,
                "No turning points. Press S to save preset, ESC/Enter to exit.",
                SCREEN_COLS_DEFAULT);
        }
        else {
            center_line_at_row(ENDGRAPH_BASE_Y + 2,
                "No turning points recorded. Press ESC/Enter.",
                SCREEN_COLS_DEFAULT);
        }
        fflush(stdout);
        for (;;) {
            int k = read_key_blocking();
            if (k == KEY_SAVE) { if (g_es_dz && g_es_cfg) prompt_and_save_current_preset(); continue; }
            if (k == KEY_ENTER || k == KEY_ESC) break;
        }
        return;
    }

    /* Panel rectangle right below the selector row, sized to the chart width. */
    Rect panel;
    panel.x = PANEL_MARGIN_X;
    panel.w = ENDGRAPH_SCREEN_COLS - PANEL_WIDTH_PAD;        /* shrink a bit for margins */
    panel.y = ENDGRAPH_BASE_Y + PANEL_TOP_OFFSET_FROM_BASE;  /* under selector row */
    panel.h = PANEL_HEIGHT;

    /* Start the selector at the last bar (usually the final snapshot). */
    int sel = hm.count - 1;
    if (sel < 0) sel = 0;

    clear_selector_row();
    draw_selector(hm.col_x[sel]);
    render_info_panel(panel, hm.node[sel] ? &hm.node[sel]->p : NULL);

    if (g_es_dz && g_es_cfg) {
        center_line_at_row(panel.y + panel.h + 1,
            "Use <- / ->    *    S to save preset    *    ESC/Enter to exit",
            SCREEN_COLS_DEFAULT);
    }
    else {
        center_line_at_row(panel.y + panel.h + 1,
            "Use <- / ->    *    ESC/Enter to exit",
            SCREEN_COLS_DEFAULT);
    }
    fflush(stdout);

    /* Event loop: move selector with arrows; exit with ESC/Enter; S to save. */
    for (;;) {
        int key = read_key_blocking();

        if (key == KEY_LEFT) {
            if (sel > 0) {
                sel -= 1;
                clear_selector_row();
                draw_selector(hm.col_x[sel]);
                render_info_panel(panel, hm.node[sel] ? &hm.node[sel]->p : NULL);
                fflush(stdout);
            }
            continue;
        }

        if (key == KEY_RIGHT) {
            if (sel + 1 < hm.count) {
                sel += 1;
                clear_selector_row();
                draw_selector(hm.col_x[sel]);
                render_info_panel(panel, hm.node[sel] ? &hm.node[sel]->p : NULL);
                fflush(stdout);
            }
            continue;
        }

        if (key == KEY_SAVE) {
            if (g_es_dz && g_es_cfg) {
                prompt_and_save_current_preset();
                /* Re-print the footer hint after the prompt. */
                if (g_es_dz && g_es_cfg) {
                    center_line_at_row(panel.y + panel.h + 1,
                        "Use <- / ->    *    S to save preset    *    ESC/Enter to exit",
                        SCREEN_COLS_DEFAULT);
                }
                else {
                    center_line_at_row(panel.y + panel.h + 1,
                        "Use <- / ->    *    ESC/Enter to exit",
                        SCREEN_COLS_DEFAULT);
                }
                fflush(stdout);
            }
            continue;
        }

        if (key == KEY_ENTER || key == KEY_ESC) {
            break;
        }
    }
}
