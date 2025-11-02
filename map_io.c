#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map_io.h"
#include "config.h"
#include "utils.h"
#include "types.h"

/*
   Map loading pipeline:
   1) Scan dimensions (validate constant line width, count rows).
   2) Allocate world grid [height][width] of Cell.
   3) Parse every character into a fully initialized Cell.
   4) Recalculate world->totalpop.

   Notes:
   - Behavior is intentionally unchanged; this is a readability/structure pass.
   - We keep all glyph semantics and population defaults identical to the original.
*/

/* ============================== Named Constants ============================== */
/* Line endings / whitespace we treat specially during parsing */
#define CH_CR              '\r'
#define CH_LF              '\n'
#define CH_TAB             '\t'
#define CH_SPACE           ' '
#define CH_TILDE           '~'

/* Map glyphs (from the text file) */
#define GLYPH_PORT         'P'
#define GLYPH_URBAN_COLD   '#'
#define GLYPH_URBAN_HOT    '&'
#define GLYPH_RURAL_HOT    '*'
#define GLYPH_RURAL_COLD   '^'

/* Error strings kept here to avoid magic literals sprinkled around */
#define ERR_INCONS_LINE    "Inconsistent line length\n"
#define ERR_FINAL_LINE     "Final line length mismatch\n"
#define ERR_BAD_DIMS       "Invalid map dimensions\n"
#define ERR_TOO_MANY_CHARS "Map parsing error: too many characters.\n"
#define ERR_INCOMPLETE     "Map parsing error: did not fill entire grid\n"

/* ============================== Small Helpers =============================== */

/* Is this character considered "sea" in the file format? */
static int is_sea_char(int ch) {
    if (ch == CH_SPACE) return 1;
    if (ch == CH_TAB)   return 1;
    if (ch == CH_CR)    return 1;
    if (ch == CH_LF)    return 1;
    if (ch == CH_TILDE) return 1;
    return 0;
}

/* ============================== World Memory ================================ */

/* Release a previously allocated world grid (safe to call on empty). */
void free_world(World* w) {
    if (w == NULL || w->grid == NULL) return;

    for (int y = 0; y < w->height; ++y) {
        free(w->grid[y]);
    }
    free(w->grid);

    w->grid = NULL;
    w->width = 0;
    w->height = 0;
}

/* Allocate world->grid as a height-by-width array of zeroed Cells. */
int allocate_world_grid(World* w) 
{
    w->grid = (Cell**)malloc(sizeof(Cell*) * (size_t)w->height);
    if (w->grid == NULL) {
        perror("malloc");
        return 0;
    }

    for (int y = 0; y < w->height; ++y) {
        w->grid[y] = (Cell*)calloc((size_t)w->width, sizeof(Cell));
        if (w->grid[y] == NULL) {
            perror("calloc");
            free_world(w);
            return 0;
        }
    }
    return 1;
}

/* ============================== Classification ============================== */
/* If you run classification passes later, these helpers are here (logic unchanged). */

static void classify_settlement_cell(Cell* c) {
    /* If sea, treat as rural for any heuristic that wants a settlement enum. */
    if (c->terrain == TERRAIN_SEA) {
        c->settlement = SETTLE_RURAL;
        return;
    }

    /* Heuristic tied to a baseline/initial population (if available). */
    if (c->pop.pop_total0 >= URBAN_POP_THRESHOLD) {
        c->settlement = SETTLE_URBAN;
    }
    else {
        c->settlement = SETTLE_RURAL;
    }
}

void classify_settlement_all(World* w) {
    for (int y = 0; y < w->height; ++y) {
        for (int x = 0; x < w->width; ++x) {
            classify_settlement_cell(&w->grid[y][x]);
        }
    }
}

/* ============================== Parsing ============================== */
/* Convert a single map character into a fully-initialized Cell.
   Complex bit explained:
     - Whitespace + '~' are SEA.
     - 'P' is a port: treated as land/urban with baseline population (DEFAULT_CELL_POP).
     - '#', '&', '*', '^' encode urban/rural and cold/hot; '&' normalizes to '#' for display.
     - Everything else on land falls back to rural/hot ('*' glyph).
*/
void parse_cell_from_char(Cell* cell, char ch)
{
    /* Reset sticky display bins to avoid first-frame flicker */
    cell->disp_i_bin = 0;
    cell->disp_d_bin = 0;

    /* Keep original char for debugging/overlays */
    cell->raw = ch;

    /* Sensible defaults; overwritten below as needed */
    cell->climate = CLIMATE_HOT;     /* irrelevant on sea; fine default elsewhere */
    cell->settlement = SETTLE_RURAL;    /* will change for urban */
    cell->disp_glyph = ' ';

    /* --- SEA: whitespace or '~' --- */
    if (is_sea_char((unsigned char)ch)) {
        cell->terrain = TERRAIN_SEA;
        cell->disp_glyph = ' ';
        cell->pop.total = 0;
        cell->pop.infected = 0;
        cell->pop.dead = 0;
        return;
    }

    /* --- PORT: its own terrain; urban by assumption --- */
    if (ch == GLYPH_PORT) {
        /* NOTE: The original code assigns MAP_PORT_CHAR / MAP_SEA_CHAR to 'terrain'.
           We keep that behavior by using the same symbol here if your config/types
           define it that way. If not, set to TERRAIN_LAND. */
#ifdef MAP_PORT_CHAR
        cell->terrain = MAP_PORT_CHAR;  /* treated as land for disease and ships */
#else
        cell->terrain = TERRAIN_LAND;
#endif
        cell->settlement = SETTLE_URBAN;
        cell->disp_glyph = 'P';
        cell->pop.total = DEFAULT_CELL_POP;
        cell->pop.infected = 0;
        cell->pop.dead = 0;
        return;
    }

    /* --- LAND: classify by the 4 symbols --- */
    cell->terrain = TERRAIN_LAND;

    if (ch == GLYPH_URBAN_COLD) {
        /* urban, cold */
        cell->settlement = SETTLE_URBAN;
        cell->climate = CLIMATE_COLD;
        cell->disp_glyph = '#';
    }
    else if (ch == GLYPH_URBAN_HOT) {
        /* urban, hot */
        cell->settlement = SETTLE_URBAN;
        cell->climate = CLIMATE_HOT;
        cell->disp_glyph = '#';  /* normalize urban to '#' for display */
    }
    else if (ch == GLYPH_RURAL_HOT) {
        /* rural, hot */
        cell->settlement = SETTLE_RURAL;
        cell->climate = CLIMATE_HOT;
        cell->disp_glyph = '*';
    }
    else if (ch == GLYPH_RURAL_COLD) {
        /* rural, cold */
        cell->settlement = SETTLE_RURAL;
        cell->climate = CLIMATE_COLD;
        cell->disp_glyph = '*';
    }
    else {
        /* Legacy / unknown land glyphs (e.g., outline chars) -> rural hot */
        cell->settlement = SETTLE_RURAL;
        cell->climate = CLIMATE_HOT;
        cell->disp_glyph = '*';
    }

    /* Baseline per-cell population for land/ports */
    cell->pop.total = DEFAULT_CELL_POP;
    cell->pop.infected = 0;
    cell->pop.dead = 0;
}

/* ============================== Dimension Scan ============================== */
/* Read the file once to detect width (constant per line) and height (line count).
   Complex bit explained:
     - We ignore CR ('\r') characters entirely.
     - A LF ('\n') ends a line. Empty lines are ignored (height increments only
       when curr_len > 0). The last line is processed if file does not end with '\n'.
     - We enforce every non-empty line has the same width.
*/
int get_map_dimensions(const char* path, int* out_width, int* out_height) {
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        perror("fopen");
        return 0;
    }
    skip_bom(f);  /* tolerate UTF-8 BOM if present */

    int width = -1;   /* -1 means “unset”; first non-empty line fixes the width */
    int height = 0;   /* number of non-empty lines */
    int curr_len = 0; /* character count in the current line */
    int ch;

    /* Consume the file byte-by-byte to control CR/LF behavior precisely */
    while ((ch = fgetc(f)) != EOF) {
        if (ch == CH_CR) {
            continue;            /* ignore carriage return (Windows CRLF) */
        }
        if (ch == CH_LF) {
            /* End of line: if line had content, validate width and count it */
            if (curr_len > 0) {
                if (width < 0) {
                    width = curr_len;     /* first non-empty line sets canonical width */
                }
                else if (curr_len != width) {
                    /* Found a line with a different length → not rectangular */
                    fprintf(stderr, ERR_INCONS_LINE);
                    fclose(f);
                    return 0;
                }
                height++;                 /* accept the line */
                curr_len = 0;             /* reset for next line */
            }
            continue;                      /* skip LF itself */
        }
        /* Any non-CR/LF byte contributes to the current line length */
        curr_len++;
    }

    /* If the last line has no trailing LF, finalize it here */
    if (curr_len > 0) {
        if (width < 0) {
            width = curr_len;             /* single-line file without LF */
        }
        else if (curr_len != width) {
            fprintf(stderr, ERR_FINAL_LINE);
            fclose(f);
            return 0;
        }
        height++;
    }

    fclose(f);

    /* Sanity: dimensions must be positive (protects later allocations) */
    if (width <= 0 || height <= 0) {
        fprintf(stderr, ERR_BAD_DIMS);
        return 0;
    }

    *out_width = width;
    *out_height = height;
    return 1;
}
/* ============================== Populate Grid ============================== */
/* Re-open the map file and fill every cell using parse_cell_from_char().
   Invariants:
     - CR/LF handling mirrors get_map_dimensions() so rows align exactly.
     - Fails if we read more than width*height “payload” characters.
     - Fails if we end before filling exactly width*height cells.
     - On failure, frees the world grid to avoid partial state.
*/
static int populate_grid_from_file(const char* path, World* w)
{
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        perror("fopen");
        free_world(w);
        return 0;
    }
    skip_bom(f);

    int x = 0;   /* column index in [0, w->width) */
    int y = 0;   /* row index    in [0, w->height) */
    int ch;

    /* Read until EOF or until we filled the last row */
    while ((ch = fgetc(f)) != EOF && y < w->height) {

        /* Treat CR/LF as end-of-line markers.
           We only advance to the next row when the current row is exactly full. */
        if (ch == CH_CR || ch == CH_LF) {
            if (x == w->width) {
                x = 0;
                y++;
            }
            continue;  /* ignore extra CR/LF beyond exact row width */
        }

        /* Guard against overflow: too many payload characters in the file */
        if (x >= w->width || y >= w->height) {
            fprintf(stderr, ERR_TOO_MANY_CHARS);
            fclose(f);
            free_world(w);
            return 0;
        }

        /* Convert one character into a fully initialized Cell */
        parse_cell_from_char(&w->grid[y][x], (char)ch);
        x++;

        /* If we just filled a row, move to the next one */
        if (x == w->width) {
            x = 0;
            y++;
        }
    }

    fclose(f);

    /* Success requires an exact fill: we must be at (x=0, y=height) */
    if (!(y == w->height && x == 0)) {
        fprintf(stderr, ERR_INCOMPLETE);
        free_world(w);
        return 0;
    }
    return 1;
}

/* ============================== Post-processing ============================== */
/* Sum living population across the whole map (ignores sea). */
void world_recalculate_totalpop(World* w) {
    unsigned long long sum = 0ULL;

    for (int y = 0; y < w->height; ++y) {
        for (int x = 0; x < w->width; ++x) {
            const Cell* c = &w->grid[y][x];
            if (c->pop.total > 0) {
                sum += (unsigned long long)c->pop.total;
            }
        }
    }

    w->totalpop = sum;
}

/* Historical helper (kept for compatibility with any external callers).
   Interprets just enough to set terrain + settlement from a char. */
static void apply_char_to_cell(Cell* c, char ch)
{
#ifdef MAP_SEA_CHAR
    c->terrain = MAP_SEA_CHAR;   /* original code used symbol-like names; keep as-is if defined */
#else
    c->terrain = TERRAIN_SEA;
#endif
    c->settlement = SETTLE_RURAL;
    c->raw = ch;

    if (is_sea_char((unsigned char)ch)) {
        return; /* sea */
    }

    if (ch == GLYPH_PORT) {
#ifdef MAP_PORT_CHAR
        c->terrain = MAP_PORT_CHAR;
#else
        c->terrain = TERRAIN_LAND;
#endif
        c->settlement = SETTLE_URBAN;
        return;
    }

    c->terrain = TERRAIN_LAND;
    c->settlement = (ch == GLYPH_URBAN_COLD) ? SETTLE_URBAN : SETTLE_RURAL;
}

/* ============================== Public Loader ============================== */
/* Main loader: compute size, allocate grid, fill cells, compute total pop.
   Contract:
     - Returns 1 on success, 0 on any failure.
     - On failure, leaves no partially-initialized world (helpers clean up).
   Rationale:
     - Early returns keep the happy path short and readable.
*/
int load_world_from_file(const char* path, World* w)
{
    /* Start from a clean slate to avoid stale pointers/sizes */
    memset(w, 0, sizeof(*w));

    int width = 0;
    int height = 0;

    /* Pass 1: scan file once to determine dimensions and enforce rectangularity */
    if (!get_map_dimensions(path, &width, &height)) {
        return 0; /* error already reported inside callee */
    }

    /* Persist dimensions for subsequent allocation and parsing */
    w->width = width;
    w->height = height;

    /* Allocate an H×W grid of zeroed Cells (fails fast and frees on error) */
    if (!allocate_world_grid(w)) {
        return 0;
    }

    /* Pass 2: re-open the file and populate every (y,x) with a decoded Cell */
    if (!populate_grid_from_file(path, w)) {
        return 0; /* callee frees grid on failure */
    }

    /* Post-process: compute world totals used by HUD and end-condition checks */
    world_recalculate_totalpop(w);
    return 1;
}
