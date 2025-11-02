/* setup.c - full-screen setup menu (arrow nav + inline edit with blinking cursor)
   Purpose:
     A tiny terminal UI to tweak Disease and Cure/SimConfig parameters before starting the sim.
     - Up/Down to move between fields
     - Space/Enter to edit a numeric/toggle field
     - Enter on [ START SIMULATION ] to accept and exit
     - Esc to quit without starting

   Notes:
     - Uses ANSI escape codes for styling and cursor control.
     - Uses _kbhit/_getch from <conio.h> to read keys without blocking.
     - Behavior is intentionally kept IDENTICAL to the original — only readability is improved.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>     /* strtod/strtol/strtoul */
#include <conio.h>      /* _getch, _kbhit */

#include "setup.h"
#include "utils.h"      /* now_ms(), sleep_ms() */
#include "config.h"     /* SimConfig / CureParams */
#include "disease.h"    /* Disease */
#include <stdbool.h>
#include "disease_io.h" 


/* ============================== Styles & Screen Control (ANSI) ============================== */
/* Plain-text names for escape codes: easier to scan + tweak if desired. */
#define S_RESET    "\x1b[0m"
#define S_REV      "\x1b[7m"       /* reverse video: white bar for selection */
#define S_EDIT     "\x1b[43;30m"   /* yellow background + black text while editing */
#define S_BOLD     "\x1b[1m"       /* bold for section headers */
#define CLR_HOME   "\x1b[H\x1b[2J\x1b[3J" /* clear whole screen + move cursor to home */

/* ============================== UI & Behavior Constants ==================================== */
#define FIELDS_CAPACITY          32      /* rows buffer: labels + values + actions */
#define EDITBUF_CAP              64      /* editable text buffer size (including '\0') */
#define LABEL_FMT                "%-24s" /* left-aligned label width used in print rows */
#define BLINK_INTERVAL_MS        400ULL  /* how often the editing cursor toggles on/off */
#define KEY_POLL_SLEEP_MS        25      /* how long to sleep when no key is pressed */

/* Allowed numeric ranges and generic clamps used by sanitizers */
#define PERCENT_MIN              0.0
#define PERCENT_MAX              100.0
#define INT_ZERO_MIN             0       /* generic non-negative clamp for ints */
#define PROB_MIN                 0.0     /* for probability-like values */
#define PROB_MAX                 1.0

/* Parsing helper: specific label prefix used to clamp the port % field */
#define PORT_PCT_LABEL_PREFIX    "Port shutdown %"

/* ============================== Keyboard Codes (Win32 _getch) =============================== */
/* We name the constants so the key-reading logic reads like English. */
#define KEY_ESC                  27
#define KEY_ENTER                '\r'
#define KEY_SPACE                ' '
#define KEY_BACKSPACE            8

/* "Extended" keys are returned as a two-byte sequence: first 0 or 224, then the code */
#define KEY_EXT_PREFIX1          0
#define KEY_EXT_PREFIX2          224
#define KEY_UP_CODE              72
#define KEY_DOWN_CODE            80

/* ============================== Field Table Types =========================================== */
/* What kind of thing each row edits/does. */
typedef enum {
    F_DBL,              /* pointer to double */
    F_INT,              /* pointer to int (signed), clamps >= 0 */
    F_UINT,             /* pointer to unsigned */
    F_TOGGLE,           /* pointer to int, toggled 0/1 */
    F_ACTION_START,     /* no pointer; activates "start" */
    F_LABEL,            /* non-interactive section header */
    F_ACTION_DEFAULTS ,  /* action: restore built-in defaults */
    F_ACTION_LOAD,       /* action: open list of saved diseases */
    F_ACTION_SAVE        /* action: save current disease to file */
} FieldKind;

/* One row in the menu: label + what it is + where the value lives (if any). */
typedef struct {
    const char* label;
    FieldKind   kind;
    void* ptr;    /* points into Disease/SimConfig (double*, int*, unsigned*) */
} SetupField;

/* Key tokens the poller returns (we hide raw integers behind names). */
typedef enum { K_NONE, K_UP, K_DOWN, K_ENTER_KEY, K_SPACE_KEY, K_ESC_KEY, K_BACK_KEY, K_CHAR } Key;

static int run_disease_picker(char out_name[], int out_cap);
static int prompt_name_and_save(const Disease* dz, const SimConfig* cfg);

/* Copy C-string with truncation and guaranteed NUL. Returns 1 on write. */
static int cstr_copy_trunc(char* dst, int cap, const char* src)
{
    if (dst == NULL || cap <= 0) return 0;
    if (src == NULL) { dst[0] = '\0'; return 1; }

    int i = 0;
    while (i < cap - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i += 1;
    }
    dst[i] = '\0';
    return 1;
}




/* ============================== Small Domain Sanitizers ===================================== */
/* Keep mutation % within [0, 100]. */
static void sanitize_mutation_inputs(SimConfig* cfg)
{
    if (cfg == NULL) return;
    if (cfg->mutation_chance_pct < PERCENT_MIN) cfg->mutation_chance_pct = PERCENT_MIN;
    if (cfg->mutation_chance_pct > PERCENT_MAX) cfg->mutation_chance_pct = PERCENT_MAX;
}

/* Clamp high-level disease inputs to safe, non-negative ranges; keep port % within [0,100]. */
static void sanitize_disease_inputs(Disease* dz)
{
    if (dz == NULL) return;

    if (dz->beta_within < 0.0) dz->beta_within = 0.0;
    if (dz->beta_neighbors < 0.0) dz->beta_neighbors = 0.0;
    if (dz->gamma_recover < 0.0) dz->gamma_recover = 0.0;
    if (dz->mu_mortality < 0.0) dz->mu_mortality = 0.0;

    if (dz->symptom_mult_within < 0.0) dz->symptom_mult_within = 0.0;
    if (dz->symptom_mult_neighbor < 0.0) dz->symptom_mult_neighbor = 0.0;
    if (dz->symptom_mult_mortality < 0.0) dz->symptom_mult_mortality = 0.0;

    if (dz->port_shutdown_pct < INT_ZERO_MIN) dz->port_shutdown_pct = INT_ZERO_MIN;
    if (dz->port_shutdown_pct > (int)PERCENT_MAX) dz->port_shutdown_pct = (int)PERCENT_MAX;
}

/* Light validation for cure params (kept identical to original behavior). */
static void sanitize_cure_inputs(SimConfig* cfg)
{
    if (cfg == NULL) return;

    CureParams* cp = &cfg->cure;

    if (cp->base_awareness_per_tick < 0.0) cp->base_awareness_per_tick = 0.0;
    if (cp->k_infected_awareness < 0.0) cp->k_infected_awareness = 0.0;
    if (cp->k_death_awareness < 0.0) cp->k_death_awareness = 0.0;
    if (cp->progress_per_awareness < 0.0) cp->progress_per_awareness = 0.0;
    if (cp->death_slowdown_k < 0.0) cp->death_slowdown_k = 0.0;

    /* probability-style clamp for post-cure recovery */
    if (cp->post_cure_gamma_recover < PROB_MIN) cp->post_cure_gamma_recover = PROB_MIN;
    if (cp->post_cure_gamma_recover > PROB_MAX) cp->post_cure_gamma_recover = PROB_MAX;
}

/* ============================== Input: Key Polling ========================================== */
/* poll_key:
   Try to read one keypress if available.
   - If no key is waiting, return K_NONE (so the main loop can blink the cursor and nap).
   - For "extended" arrow keys, _getch returns a prefix (0 or 224) then a second byte.
     We translate those into K_UP / K_DOWN.
   - For normal characters, we return K_CHAR and also set *out_ch to the actual char.
*/
static Key poll_key(int* out_ch)
{
    if (_kbhit() == 0) {
        return K_NONE;
    }

    int ch = _getch();

    if (ch == KEY_ESC) {
        return K_ESC_KEY;
    }

    if (ch == KEY_ENTER) {
        return K_ENTER_KEY;
    }

    if (ch == KEY_SPACE) {
        return K_SPACE_KEY;
    }

    if (ch == KEY_BACKSPACE) {
        return K_BACK_KEY;
    }

    /* Extended key sequence: prefix then actual key code */
    if (ch == KEY_EXT_PREFIX1 || ch == KEY_EXT_PREFIX2) {
        int ch2 = _getch();
        if (ch2 == KEY_UP_CODE)   return K_UP;
        if (ch2 == KEY_DOWN_CODE) return K_DOWN;
        return K_NONE;
    }

    if (out_ch != NULL) {
        *out_ch = ch;
    }
    return K_CHAR;
}

/* ============================== Small Helpers =============================================== */
/* Returns 0 for headers (F_LABEL), 1 for actual interactive rows. */
static int is_interactive(const SetupField* fld) {
    if (fld == NULL) return 0;
    if (fld->kind == F_LABEL) return 0;
    return 1;
}

/* build_fields:
   Fill an array of SetupField entries that drives the on-screen table.
   Grouping is purely visual. Each row knows:
     - its label string
     - its kind (double/int/uint/toggle/action/label)
     - where to read/write the value (ptr) if applicable
*/
static void build_fields(Disease* dz, SimConfig* cfg, SetupField* out, int* outN)
{
    if (dz == NULL || cfg == NULL || out == NULL || outN == NULL) return;

    int i = 0;

    /* ---- Section: Disease basics ---- */
    out[i++] = (SetupField){ "~ Disease ~", F_LABEL, NULL };

    out[i++] = (SetupField){ "Start Inside spread         ", F_DBL,  &dz->beta_within };
    out[i++] = (SetupField){ "Start Neighbor spread       ", F_DBL,  &dz->beta_neighbors };
    out[i++] = (SetupField){ "Recovery rate               ", F_DBL,  &dz->gamma_recover };
    out[i++] = (SetupField){ "Start Fatality rate         ", F_DBL,  &dz->mu_mortality };

    /* ---- Section: Multipliers & Ports ---- */
    out[i++] = (SetupField){ "~ Multipliers & Ports ~", F_LABEL, NULL };

    out[i++] = (SetupField){ "Inside spread multiplayer   ", F_DBL,  &dz->symptom_mult_within };
    out[i++] = (SetupField){ "Neighbor spread multiplayer ", F_DBL,  &dz->symptom_mult_neighbor };
    out[i++] = (SetupField){ "Fatality multiplayer        ", F_DBL,  &dz->symptom_mult_mortality };
    out[i++] = (SetupField){ "Deaths slow spread (k)      ", F_DBL,  &dz->death_supp_k };
    out[i++] = (SetupField){ "Port shutdown %             ", F_INT,  &dz->port_shutdown_pct };
    out[i++] = (SetupField){ "Random seed                 ", F_UINT, &dz->rng_seed };
    out[i++] = (SetupField){ "Mutations                   ", F_TOGGLE, &dz->mutations_enabled };
    out[i++] = (SetupField){ "Mutation chance per tick (%) ", F_DBL,  &cfg->mutation_chance_pct };

    /* ---- Section: Awareness & Cure ---- */
    out[i++] = (SetupField){ "~ Awareness & Cure ~", F_LABEL, NULL };

    out[i++] = (SetupField){ "Awareness: base per tick    ", F_DBL, &cfg->cure.base_awareness_per_tick };
    out[i++] = (SetupField){ "Awareness: infected weight  ", F_DBL, &cfg->cure.k_infected_awareness };
    out[i++] = (SetupField){ "Awareness: deaths weight    ", F_DBL, &cfg->cure.k_death_awareness };
    out[i++] = (SetupField){ "Cure: progress per awareness", F_DBL, &cfg->cure.progress_per_awareness };
    out[i++] = (SetupField){ "Cure: death slowdown k      ", F_DBL, &cfg->cure.death_slowdown_k };
    out[i++] = (SetupField){ "After-cure: recovery gamma  ", F_DBL, &cfg->cure.post_cure_gamma_recover };
    /* ---- Save and load ---- */
    out[i++] = (SetupField){ "[ LOAD SAVED DISEASE ]", F_ACTION_LOAD, NULL };
    out[i++] = (SetupField){ "[ SAVE CURRENT DISEASE ]", F_ACTION_SAVE, NULL };

    /* ---- Actions ---- */
    out[i++] = (SetupField){ "[ RESTORE DEFAULTS ]", F_ACTION_DEFAULTS, NULL };
    out[i++] = (SetupField){ "[ START SIMULATION ]", F_ACTION_START,    NULL };

    *outN = i;
}

/* ============================== Rendering ==================================================== */
/* Narrow, explicit printing helpers keep draw_setup_screen() short and readable. */
static void print_row_double(const char* label, double v) { printf(LABEL_FMT " : %.6g\n", label, v); }
static void print_row_int(const char* label, int    v) { printf(LABEL_FMT " : %d\n", label, v); }
static void print_row_uint(const char* label, unsigned v) { printf(LABEL_FMT " : %u\n", label, v); }

static void print_row_toggle(const char* label, int v)
{
    const char* state = (v != 0) ? "on" : "off";
    printf(LABEL_FMT " : %s\n", label, state);
}

/* draw_setup_screen:
   Draws the entire screen:
     - Clears the terminal
     - Prints title + instructions
     - Prints each row (headers, fields, actions)
     - If a row is selected, it’s highlighted (reverse video). If editing, it’s yellow.
     - If editing, prints an "Edit: ..." line at the bottom with blinking cursor.

   Complex bit explained:
     We render full screen each time to keep logic dead simple.
     The “blink” is simulated by toggling an underscore on/off every BLINK_INTERVAL_MS.
*/
static void draw_setup_screen(const SetupField* fields, int n_fields,
    int sel, int editing,
    const char* editbuf, int blink_on)
{
    fputs(CLR_HOME, stdout);
    fflush(stdout);


    puts("=== Setup: Disease & Cure Parameters ===");
    puts("Use \x1b[1mUp/Down\x1b[0m to move, \x1b[1mSpace/Enter\x1b[0m to edit, Enter on Start, Esc to quit\n");

    for (int i = 0; i < n_fields; ++i) {
        const char* label = fields[i].label;
        const char* style_on = NULL;

        /* Section headers (non-interactive) */
        if (fields[i].kind == F_LABEL) {
            printf(S_BOLD "%s" S_RESET "\n\n", label);
            continue;
        }

        /* Selection highlight: reverse video when navigating; yellow when editing */
        if (i == sel) {
            if (editing == 0) style_on = S_REV;
            else              style_on = S_EDIT;
        }
        if (style_on != NULL) fputs(style_on, stdout);

        /* Value rows + actions */
        if (fields[i].kind == F_DBL) {
            print_row_double(label, *(double*)fields[i].ptr);
        }
        else if (fields[i].kind == F_INT) {
            print_row_int(label, *(int*)fields[i].ptr);
        }
        else if (fields[i].kind == F_UINT) {
            print_row_uint(label, *(unsigned*)fields[i].ptr);
        }
        else if (fields[i].kind == F_TOGGLE) {
            print_row_toggle(label, *(int*)fields[i].ptr);
        }
        else if (fields[i].kind == F_ACTION_START) {
            puts("");
            puts("[ START SIMULATION ]");
        }
        else if (fields[i].kind == F_ACTION_DEFAULTS) {
            puts("");
            puts("[ RESTORE DEFAULTS ]");
        }
        else if (fields[i].kind == F_ACTION_LOAD) {
            puts("");
            puts("[ LOAD SAVED DISEASE ]");
        }
        else if (fields[i].kind == F_ACTION_SAVE) {
            puts("");
            puts("[ SAVE CURRENT DISEASE ]");
        }
        if (style_on != NULL) fputs(S_RESET, stdout);
    }

    /* Inline edit input at bottom with blinking cursor */
    if (editing != 0) {
        puts("");
        fputs("Edit: ", stdout);
        fputs(editbuf, stdout);
        if (blink_on != 0) fputs("_", stdout);
        else               fputs(" ", stdout);
    }

    fflush(stdout);
}

/* ============================== Editing Helpers ============================================= */
/* begin_edit:
   Seed the edit buffer from the current value of the selected field.
   We format doubles with %.6g so large/small values display compactly.
*/
static void begin_edit(const SetupField* fld, char* buf, int* len)
{
    if (fld == NULL || buf == NULL || len == NULL) return;

    buf[0] = '\0';

    if (fld->kind == F_DBL) {
        double v = *(double*)fld->ptr;
        (void)snprintf(buf, EDITBUF_CAP, "%.6g", v);
    }
    else if (fld->kind == F_INT) {
        int v = *(int*)fld->ptr;
        (void)snprintf(buf, EDITBUF_CAP, "%d", v);
    }
    else if (fld->kind == F_UINT) {
        unsigned v = *(unsigned*)fld->ptr;
        (void)snprintf(buf, EDITBUF_CAP, "%u", v);
    }

    *len = (int)strlen(buf);
}

/* commit_edit:
   Parse the edit buffer and write the value back into the field (if valid).
   - Doubles are clamped to >= 0.
   - Ints are clamped to >= 0, and the specific "Port shutdown %" field is clamped to [0,100].
   - Unsigned just parses as base-10 and assigns.
   We keep the original behavior *exactly*, just with named constants and comments.
*/
static void commit_edit(const SetupField* fld, const char* buf)
{
    if (fld == NULL || buf == NULL) return;

    if (fld->kind == F_DBL) {
        char* endp = NULL;
        double v = strtod(buf, &endp);
        if (endp != buf) {
            double* p = (double*)fld->ptr;
            *p = v;
            if (*p < 0.0) *p = 0.0;
        }
    }
    else if (fld->kind == F_INT) {
        char* endp = NULL;
        long v = strtol(buf, &endp, 10);
        if (endp != buf) {
            int* p = (int*)fld->ptr;
            *p = (int)v;
            if (*p < INT_ZERO_MIN) *p = INT_ZERO_MIN;

            /* Match original: clamp the specific "Port shutdown %" field to [0,100] */
            if (strncmp(fld->label, PORT_PCT_LABEL_PREFIX, (int)strlen(PORT_PCT_LABEL_PREFIX)) == 0) {
                if (*p > (int)PERCENT_MAX) *p = (int)PERCENT_MAX;
            }
        }
    }
    else if (fld->kind == F_UINT) {
        char* endp = NULL;
        unsigned long v = strtoul(buf, &endp, 10);
        if (endp != buf) {
            unsigned* p = (unsigned*)fld->ptr;
            *p = (unsigned)v;
        }
    }
}

/* edit_push_char:
   Push one typed character into the edit buffer if it’s valid for numeric input.
   Valid chars:
     - digits 0..9
     - decimal point '.'
     - sign '+' or '-'
*/
static void edit_push_char(int ch, char* buf, int* len)
{
    if (buf == NULL || len == NULL) return;

    /* Keep one slot for the trailing '\0' */
    if (*len >= (EDITBUF_CAP - 1)) return;

    int is_digit = 0;
    if (ch >= '0' && ch <= '9') is_digit = 1;

    /* Accept digits, '.', '-', '+' */
    if (is_digit || ch == '.' || ch == '-' || ch == '+') {
        buf[*len] = (char)ch;
        *len = *len + 1;
        buf[*len] = '\0';
    }
}

/* ============================== Public Entry Point ========================================== */
/* setup_menu:
   The main loop of the setup UI. It:
     - Builds the field table, draws the screen
     - Lets the user navigate rows and edit values
     - On [ RESTORE DEFAULTS ], calls disease_init_defaults() + simconfig_init_defaults()
     - On [ START SIMULATION ], sanitizes values and returns 1
     - On Esc, returns 0

   Blinking logic:
     - While editing, we toggle a "_" cursor on/off every BLINK_INTERVAL_MS.
     - We only redraw when the blink flips or user input changes something.
*/
int setup_menu(Disease* dz, SimConfig* cfg)
{
    if (dz == NULL) return 0;
    if (cfg == NULL) return 0;

    SetupField fields[FIELDS_CAPACITY];   /* room for all rows + headers + actions */
    int n_fields = 0;
    build_fields(dz, cfg, fields, &n_fields);

    int sel = 0;                 /* currently selected row index */
    bool editing = false;             /* 0 = not editing, 1 = editing text */
    char editbuf[EDITBUF_CAP];   /* input buffer while editing */
    int buflen = 0;

    int blink_on = 1;            /* blinky "_" state */
    unsigned long long t_last = now_ms();

    /* Initial draw */
    draw_setup_screen(fields, n_fields, sel, editing, "", blink_on);

    while (1) {
        /* Blink the cursor while editing (every BLINK_INTERVAL_MS); redraw only when it flips. */
        if (editing != false) {
            unsigned long long t_now = now_ms();
            if (t_now - t_last >= BLINK_INTERVAL_MS) {
                blink_on = (blink_on == 0) ? 1 : 0;
                t_last = t_now;
                draw_setup_screen(fields, n_fields, sel, editing, editbuf, blink_on);
            }
        }

        /* Poll key (non-blocking). If none, nap briefly and continue. */
        int ch_raw = 0;
        Key k = poll_key(&ch_raw);
        if (k == K_NONE) {
            sleep_ms(KEY_POLL_SLEEP_MS);
            continue;
        }

        if (editing == false) 
        {
            /* ======================= Navigation mode ======================= */
            if (k == K_ESC_KEY) {
                return 0;
            }

            if (k == K_UP) {
                int tries = 0;
                do {
                    sel -= 1;
                    if (sel < 0) sel = n_fields - 1;          /* wrap to the bottom */
                    tries += 1;
                } while (!is_interactive(&fields[sel]) && tries < n_fields);
                draw_setup_screen(fields, n_fields, sel, 0, "", 0);
                continue;
            }

            if (k == K_DOWN) {
                int tries = 0;
                do {
                    sel += 1;
                    if (sel >= n_fields) sel = 0;             /* wrap to the top */
                    tries += 1;
                } while (!is_interactive(&fields[sel]) && tries < n_fields);
                draw_setup_screen(fields, n_fields, sel, 0, "", 0);
                continue;
            }

            if (k == K_SPACE_KEY || k == K_ENTER_KEY) 
            {
                if (fields[sel].kind == F_TOGGLE) {
                    /* Flip 0<->1 for toggles (Mutations). */
                    int* p = (int*)fields[sel].ptr;
                    *p = (*p == 0) ? 1 : 0;
                    draw_setup_screen(fields, n_fields, sel, 0, "", 0);
                    continue;
                }

                if (fields[sel].kind == F_ACTION_DEFAULTS) {
                    /* Restore defaults for both domains and redraw */
                    disease_init_defaults(dz);
                    simconfig_init_defaults(cfg);
                    draw_setup_screen(fields, n_fields, sel, 0, "", 0);
                    continue;
                }

                if (fields[sel].kind == F_ACTION_START) {
                    /* Final cleanup before returning success */
                    sanitize_disease_inputs(dz);
                    sanitize_cure_inputs(cfg);
                    sanitize_mutation_inputs(cfg);
                    return 1;
                }

                if (fields[sel].kind == F_ACTION_LOAD) {
                    char chosen[DISEASE_NAME_MAX] = { 0 };
                    if (run_disease_picker(chosen, (int)sizeof(chosen))) {
                        if (disease_load_by_name(DISEASES_FILE_PATH, chosen, dz, cfg)) {
                            sanitize_disease_inputs(dz);
                            sanitize_cure_inputs(cfg);
                            sanitize_mutation_inputs(cfg);
                        }
                    }
                    draw_setup_screen(fields, n_fields, sel, 0, "", 0);
                    continue;
                }

                if (fields[sel].kind == F_ACTION_SAVE) {
                    (void)prompt_name_and_save(dz, cfg); /* handles UI + write + tiny message */
                    draw_setup_screen(fields, n_fields, sel, 0, "", 0);
                    continue;
                }

                /* Edit the filed if no if we are not on any special field */
                begin_edit(&fields[sel], editbuf, &buflen);
                editing = true;
                blink_on = 1;
                t_last = now_ms();
                draw_setup_screen(fields, n_fields, sel, 1, editbuf, blink_on);
                continue;
            }

            /* also allow S to start quickly (kept as-is) */
            if (k == K_CHAR) {
                if (ch_raw == 's' || ch_raw == 'S') {
                    sanitize_disease_inputs(dz);
                    sanitize_cure_inputs(cfg);
                    sanitize_mutation_inputs(cfg);
                    return 1;
                }
            }
        }
		else // editing == true
        {
            /* ========================= Editing mode ======================== */
            if (k == K_ESC_KEY) {
                /* cancel editing */
                editing = false;
                draw_setup_screen(fields, n_fields, sel, 0, "", 0);
                continue;
            }

            if (k == K_ENTER_KEY) {
                /* commit edit */
                commit_edit(&fields[sel], editbuf);
                editing = false;
                draw_setup_screen(fields, n_fields, sel, 0, "", 0);
                continue;
            }

            if (k == K_BACK_KEY) {
                if (buflen > 0) {
                    buflen -= 1;
                    editbuf[buflen] = '\0';
                    draw_setup_screen(fields, n_fields, sel, 1, editbuf, blink_on);
                }
                continue;
            }

            if (k == K_CHAR) {
                /* accept only digits, sign, and dot */
                edit_push_char(ch_raw, editbuf, &buflen);
                draw_setup_screen(fields, n_fields, sel, 1, editbuf, blink_on);
                continue;
            }
        }
    }
}

/* run_disease_picker:
   Simple blocking list: Up/Down to choose, Enter selects, Esc cancels.
   Returns 1 if a name was chosen and copied into out_name.
*/
static int run_disease_picker(char out_name[], int out_cap)
{
    char names[DISEASE_LIST_MAX][DISEASE_NAME_MAX];
    int n = 0;
    if (!disease_list_names(DISEASES_FILE_PATH, names, DISEASE_LIST_MAX, &n)) {
        return 0;
    }
    if (n <= 0) return 0;

    int sel = 0;
    while (1) {
        /* Render a very small modal list (reuse your clear + print style) */
        fputs(CLR_HOME, stdout);
        fflush(stdout);
        puts("=== Load Saved Disease ===");
        puts("Up/Down to choose, Enter to load, Esc to cancel\n");
        for (int i = 0; i < n; ++i) {
            if (i == sel) fputs(S_REV, stdout);
            printf("  %s\n", names[i]);
            if (i == sel) fputs(S_RESET, stdout);
        }
        fflush(stdout);

        int ch_raw = 0;
        Key k = poll_key(&ch_raw);
        if (k == K_NONE) { sleep_ms(KEY_POLL_SLEEP_MS); continue; }
        if (k == K_ESC_KEY) return 0;
        if (k == K_UP) { sel -= 1; if (sel < 0) sel = n - 1; continue; }
        if (k == K_DOWN) { sel += 1; if (sel >= n) sel = 0;   continue; }
        if (k == K_ENTER_KEY) {
            (void)cstr_copy_trunc(out_name, out_cap, names[sel]);
            return 1;
        }
    }
}

/* prompt_name_and_save:
   Lets user type a preset name; Enter saves, Esc cancels.
   Allowed chars: letters, digits, '_' and '-'.
*/
static int prompt_name_and_save(const Disease* dz, const SimConfig* cfg)
{
    char namebuf[DISEASE_NAME_MAX] = { 0 };
    int len = 0;

    while (1) {
        fputs(CLR_HOME, stdout);
        puts("=== Save Current Disease ===");
        puts("Type a name (A-Z, a-z, 0-9, _ or -). Enter to save, Esc to cancel.\n");
        printf("Name: %s_\n", namebuf);
        fflush(stdout);

        int ch_raw = 0;
        Key k = poll_key(&ch_raw);
        if (k == K_NONE) { sleep_ms(KEY_POLL_SLEEP_MS); continue; }
        if (k == K_ESC_KEY) return 0;

        if (k == K_BACK_KEY) {
            if (len > 0) {
                len -= 1;
                namebuf[len] = '\0';
            }
            continue;
        }

        if (k == K_ENTER_KEY) {
            if (len > 0) {
                /* NEW: call the overwrite/rename popup wrapper.
                   It may update namebuf if the user chooses Rename. */
                int ok = disease_save_with_prompt(DISEASES_FILE_PATH, namebuf, dz, cfg);
                if (ok != 0) {
                    /* Optional: brief confirmation */
                    printf("Saved preset: %s\n", namebuf);
                }
                else {
                    printf("Save aborted.\n");
                }
                return ok;
            }
            continue;
        }

        if (k == K_CHAR) {
            int ok_char = 0;
            if ((ch_raw >= 'a' && ch_raw <= 'z') || (ch_raw >= 'A' && ch_raw <= 'Z')) ok_char = 1;
            if (ch_raw >= '0' && ch_raw <= '9') ok_char = 1;
            if (ch_raw == '_' || ch_raw == '-') ok_char = 1;

            if (ok_char != 0) {
                if (len < (DISEASE_NAME_MAX - 1)) {
                    namebuf[len] = (char)ch_raw;
                    len = len + 1;
                    namebuf[len] = '\0';
                }
            }
        }
    }
}

