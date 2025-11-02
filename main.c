// ===================== main.c — Plague simulator entry point =====================
#include <stdio.h>
#include <time.h>

#include "config.h"
#include "types.h"

#include "rng.h"
#include "utils.h"
#include "ansi.h"
#include "console_win.h"

#include "map_io.h"
#include "disease.h"
#include "world.h"
#include "render.h"
#include "ports.h"
#include "ships.h"
#include "hud.h"
#include "symptoms.h"
#include "setup.h"
#include "cure.h"
#include "end_condition.h"
#include "end_screen.h"
#include "turnpoints.h"
#include "end_graph.h"   /* (fixed) used by end screen */
#include "patient_zero.h"

// ===================== Tunables & Defaults (single source of truth) =====================

/* Input map file path */
#ifndef MAP_FILE_PATH
#define MAP_FILE_PATH                "map.txt"
#endif

/* Extra console rows for HUD below the map */
#ifndef HUD_EXTRA_ROWS
#define HUD_EXTRA_ROWS               12
#endif

/* Symptom mutation cadence (internal engine ticks between attempts) */
#ifndef MUTATION_PERIOD_TICKS
#define MUTATION_PERIOD_TICKS        1
#endif

/* Default population to ensure at 'P' port tiles if uninitialized */
#ifndef DEFAULT_PORT_POP
#define DEFAULT_PORT_POP             150
#endif

/* End-screen grace window after an end condition is first detected */
#ifndef END_GRACE_TICKS
#define END_GRACE_TICKS              100   /* use 20 for snappier endings */
#endif

/* If TARGET_FPS > 0, plateau is computed as ~seconds * FPS. Otherwise, fallback ticks */
#ifndef DEFAULT_PLATEAU_SECONDS
#define DEFAULT_PLATEAU_SECONDS      8
#endif
#ifndef DEFAULT_PLATEAU_TICKS_FALLBACK
#define DEFAULT_PLATEAU_TICKS_FALLBACK 480
#endif

/* Cure green overlay: time to fade from red→green once cure is active (seconds) */
#ifndef CURE_FADE_SECONDS
#define CURE_FADE_SECONDS            5.0f
#endif

/* “Zero infected” confirmation time in recovery mode (seconds) */
#ifndef RECOVERY_ZERO_LIMIT_SECONDS
#define RECOVERY_ZERO_LIMIT_SECONDS  1
#endif

/* Minimal FPS if TARGET_FPS is 0 or undefined at compile time */
#ifndef MIN_RUN_FPS
#define MIN_RUN_FPS                  1u
#endif

#ifndef ERADICATE_OVERRIDE_FRAC
#define ERADICATE_OVERRIDE_FRAC  0.005 // its by %
#endif

static int g_baseline_total_pop = 0;  /* captured after setup; used for 0.5% override */

// ===================== File-scope state =====================

static SimConfig g_cfg;    /* edited in setup menu (awareness/cure params, etc.) */
static CureState g_cure;   /* runtime cure state (awareness/progress/active)   */

/* Global so end_screen.c's `extern TurnTracker g_tp;` can link */
TurnTracker g_tp;

// ===================== Small helpers =====================

/* Title + first frame so the player doesn't stare at a blank screen */
static void draw_first_frame(const World* w, int tick, const Disease* dz) {
    /* Static title/header area (from your HUD/Render modules) */
    draw_static_header(w);

    /* First full frame + HUD */
    draw_frame_incremental(w);
    draw_hud_line(w, tick, dz, &g_cure);
    draw_symptoms_line(w, dz);
    fflush(stdout);
}

/* Handle console resizing cleanly (buffer, cache, overlays, redraw) */
static void handle_resize(const World* w, int tick, const Disease* dz) {
#ifdef _WIN32
    system("cls"); /* simple + reliable on Windows */
#else
    fputs("\x1b[2J\x1b[H", stdout);
#endif
    ensure_console_buffer_at_least(w->width, w->height + HUD_EXTRA_ROWS);
    invalidate_frame_cache(w);
    reset_ship_overlays();

    draw_static_header(w);
    draw_frame_incremental(w);
    draw_hud_line(w, tick, dz, &g_cure);
    draw_symptoms_line(w, dz);
    fflush(stdout);
}

/* Scan totals over land and ports (skip pure sea) — used for end-condition logic */
static void totals_alive_infected_land(const World* w, int* out_alive, int* out_infected) {
    int alive = 0;
    int infected = 0;

    if (w != NULL) {
        for (int y = 0; y < w->height; y++) {
            for (int x = 0; x < w->width; x++) {
                const Cell* c = &w->grid[y][x];

                int is_land_or_port = 0;
                if (c->terrain == TERRAIN_LAND) {
                    is_land_or_port = 1;
                }
                if (c->raw == 'P') { /* ports count as land for totals */
                    is_land_or_port = 1;
                }

                if (is_land_or_port == 1) {
                    if (c->pop.total > 0) {
                        alive += c->pop.total;
                    }
                    if (c->pop.infected > 0) {
                        infected += c->pop.infected;
                    }
                }
            }
        }
    }

    if (alive < 0)   alive = 0;
    if (infected < 0) infected = 0;

    if (out_alive)    *out_alive = alive;
    if (out_infected) *out_infected = infected;
}

static int read_key_blocking(void)
{
    for (;;) {
        int ch = getchar();        /* waits for input; line-buffered */
        if (ch == EOF) {
            return 'C';            /* safe default: continue */
        }
        if (ch == 's' || ch == 'S') {
            return 'S';
        }
        if (ch == 'c' || ch == 'C') {
            return 'C';
        }
        /* ignore everything else (including newline) */
    }
}

// ===================== Entry point =====================

int main(void)
{
    /* Large stdout buffer for smoother rendering (reduces flush stalls) */
    static char bigbuf[1 << 20];
    setvbuf(stdout, bigbuf, _IOFBF, sizeof(bigbuf));

    /* Enable ANSI/VT mode on Windows */
    enable_vt_mode();

    /* Clear scrollback + screen, move cursor home */
    fputs("\x1b[3J", stdout);
    fputs("\x1b[2J", stdout);
    fputs("\x1b[H", stdout);

    /* ----- Load world ----- */
    World w;
    if (load_world_from_file(MAP_FILE_PATH, &w) == 0) {
        fprintf(stderr, "Failed to load %s\n", MAP_FILE_PATH);
        return 1;
    }
    classify_settlement_all(&w);  /* derive rural/urban from glyphs */

    /* Console buffer with headroom for HUD bars */
    ensure_console_buffer_at_least(w.width, w.height + HUD_EXTRA_ROWS);
    alloc_frame_cache(&w);

    /* Hide cursor for animation */
    fputs(HIDE_CURSOR, stdout);

    /* ----- Disease setup (defaults → interactive setup) ----- */
    Disease dz;
    disease_init_defaults(&dz);

    /* Prefill cure/awareness defaults BEFORE the menu (user edits them) */
    simconfig_init_defaults(&g_cfg);

    /* Setup menu (blocking). Return 0 = user canceled. */
    if (setup_menu(&dz, &g_cfg) == 0) {
        fputs(SHOW_CURSOR ANSI_RESET "\n", stdout);
        free_frame_cache();
        free_world(&w);
        return 0;
    }

    /* Seed RNG chosen by the user in setup */
    rng_seed(dz.rng_seed);

    /* Initialize symptom system (root active; countdown set) */
    symptoms_init(&dz.symptoms, MUTATION_PERIOD_TICKS);

    /* Precompute ship routes (doesn't depend on being infected) */
    generate_port_routes(&w);

    /* Make sure ports have people if map omitted counts */
    ensure_port_population(&w, DEFAULT_PORT_POP);

    /* Capture baseline total population once (land+ports) for 0.5% override */
    {
        int alive0 = 0;
        int inf0 = 0;
        totals_alive_infected_land(&w, &alive0, &inf0);
        if (alive0 > 0) {
            g_baseline_total_pop = alive0;
        }
        else {
            g_baseline_total_pop = 0;  /* avoid div-by-zero; disables override if empty */
        }
    }

    /* Let the user pick patient zero; ESC returns to Setup.
       We loop so the player can tweak settings and try again. */
    while (1) {
        int ok = pick_patient_zero(&w, &dz);
        if (ok == 1) {
            /* user confirmed a valid land/port cell */
            break;
        }

        /* ESC: go back to Setup; if they cancel Setup, exit cleanly */
        if (setup_menu(&dz, &g_cfg) == 0) {
            fputs(SHOW_CURSOR ANSI_RESET "\n", stdout);
            free_frame_cache();
            free_world(&w);
            tp_free(&g_tp);
            return 0;
        }

        /* Apply updated settings from Setup before re-showing picker */
        rng_seed(dz.rng_seed);
        symptoms_init(&dz.symptoms, MUTATION_PERIOD_TICKS);
    }

    /* Initialize cure runtime state (AFTER menu so cfg changes persist) */
    cure_init(&g_cure);
    tp_init(&g_tp);

    /* ----- End-condition tracker (plateau + caps) ----- */
    EndConditionTracker endT;

    /* Compute plateau limit (ticks) from FPS if available; otherwise use fallback. */
    int plateau_limit_ticks;
    if (TARGET_FPS > 0) {
        plateau_limit_ticks = TARGET_FPS * DEFAULT_PLATEAU_SECONDS; /* ~N seconds without growth */
    }
    else {
        plateau_limit_ticks = DEFAULT_PLATEAU_TICKS_FALLBACK;
    }


    /* No hard cap on total ticks by default (0 = unlimited) */
    int max_ticks_cap = 0;

    endcondition_init(&endT, plateau_limit_ticks, max_ticks_cap);

    /* Short “grace” window to let visuals settle before switching to end screen */
    int  end_grace_left = -1;          /* -1 = no countdown active */
    EndReason end_latched = END_NONE;  /* reason we’ll show on the end screen */

    /* --- Visuals: cure fade red→green --- */
    float cure_fade = 0.0f;    /* current fade amount [0..1] */
    float cure_fade_step;
    if (TARGET_FPS > 0) {
        cure_fade_step = 1.0f / (TARGET_FPS * CURE_FADE_SECONDS);
    }
    else {
        /* Fallback: ~5 seconds at ~60FPS equivalent */
        cure_fade_step = 1.0f / 300.0f;
    }


    /* Frame rate timing (convert FPS → ms per frame) */
    unsigned fps;
    if (TARGET_FPS > 0u) {
        fps = (unsigned)TARGET_FPS;
    }
    else {
        fps = MIN_RUN_FPS;
    }

    unsigned frame_ms = (unsigned)(1000.0 / (double)fps + 0.5);

    /* First frame (header + map + HUD) */
    draw_first_frame(&w, 0, &dz);

    // ===================== Main simulation loop =====================
    {
        int t = 1;
        while (1) 
        {
            unsigned long long t0 = now_ms();

            /* Handle console resize quickly; then force a redraw */
            if (check_console_resize() != 0) {
                handle_resize(&w, t, &dz);
            }

            /* 1) Mutations happen BEFORE world step so effects apply immediately */
            if (dz.mutations_enabled != 0) {
                /* Clamp percent to [0,100] but keep as double to keep decimals */
                double pct = g_cfg.mutation_chance_pct;
                if (pct < 0.0) {
                    pct = 0.0;
                }
                if (pct > 100.0) {
                    pct = 100.0;
                }

                /* Bernoulli(p): draw u in [0,1), mutate if u < pct/100 */
                double u = rng01();
                double p = pct / 100.0;
                if (u < p) {
                    SymptomId new_id;
                    int got_new = symptoms_mutation_tick(&dz.symptoms, &new_id);
                    if (got_new != 0) {
                        HudStats s;
                        compute_world_stats(&w, &s);
                        tp_note_symptom(&g_tp, t, (int)new_id,
                            s.alive_pop, s.infected_pop, s.dead_pop, s.total_pop);
                    }
                }
            }

            /* Rebuild effective disease multipliers from active symptoms (no compounding) */
            disease_refresh_effective_params_from_symptoms(&dz);

            /* 2) Cure system runs BEFORE world step so activation halts spread this tick */
            cure_tick_update(&g_cure, &g_cfg.cure, &w);

            if (cure_maybe_activate(&g_cure, &dz, &g_cfg.cure, &w) != 0) {
                /* Enter recovery mode: wait until infected==0 for ~N seconds */
                int zero_limit_ticks;
                if (TARGET_FPS > 0) {
                    zero_limit_ticks = TARGET_FPS * RECOVERY_ZERO_LIMIT_SECONDS;
                }
                else {
                    zero_limit_ticks = 30; /* safe fallback ~1s at ~30fps */
                }
                endcondition_set_recovery(&endT, 1, zero_limit_ticks);

                HudStats s;
                compute_world_stats(&w, &s);
                tp_note_cure_active(&g_tp, t,
                    s.alive_pop, s.infected_pop, s.dead_pop, s.total_pop);
            }

            /* VISUAL ONLY: animate red → green while cure is active */
            if (cure_is_active(&g_cure) == 1) 
            {
                if (cure_fade < 1.0f) {
                    cure_fade += cure_fade_step;
                    if (cure_fade > 1.0f) {
                        cure_fade = 1.0f;
                    }
                }
            }
            render_set_cure_fade(cure_fade);

            /* 3) Advance simulation for this tick (infection, deaths, ports) */
            step_world(&w, &dz);

            /* 4) Draw updated world state */
            draw_frame_incremental(&w);

            /* 5) Ships AFTER disease & ports updated */
            update_ship_routes(&w);
            if (ships_consume_first_infected_flag() != 0) {
                HudStats s;
                compute_world_stats(&w, &s);
                tp_note_first_infected_ship(&g_tp, t,
                    s.alive_pop, s.infected_pop, s.dead_pop, s.total_pop);
            }

            /* 6) HUD last (so numbers match the frame you’re seeing) */
            draw_hud_line(&w, t, &dz, &g_cure);
            draw_symptoms_line(&w, &dz);
            fflush(stdout);

            /* Record thresholds crossed this tick (10/20/50/80/100%) */
            {
                HudStats s;
                compute_world_stats(&w, &s);
                tp_update_thresholds(&g_tp, t,
                    s.alive_pop, s.infected_pop, s.dead_pop, s.total_pop,
                    cure_is_active(&g_cure));
            }

            /* --- End-condition check + short grace window --- */
            /* --- End-condition check + short grace window --- */
            {
                int total_alive = 0;
                int total_infected = 0;
                totals_alive_infected_land(&w, &total_alive, &total_infected);

                /* Ask the end-condition engine for the current status */
                EndReason why = endcondition_update(&endT, total_alive, total_infected, &dz, t);

                /* Soft-stall prompt (pre-cure): user can decide to stop or continue */
                if (endT.soft_stall_hint == 1 && end_grace_left < 0 && why == END_NONE) {
                    endT.soft_stall_hint = 0;  /* consume the hint */

                    /* Simple console message */
                    printf("\n[STALL] Spread seems flat. Press S=stop or C=continue: ");
                    fflush(stdout);

                    /* Read a single key (blocking) */
                    int ch = getchar();

                    if (ch == 'S' || ch == 's') {
                        /* Convert soft stall into a real ending */
                        end_latched = END_STALLED;
                        end_grace_left = END_GRACE_TICKS;
                    }
                    else {
                        /* Continue the run; optionally refresh HUD to clear the message */
                        draw_hud_line(&w, t, &dz, &g_cure);
                        draw_symptoms_line(&w, &dz);
                        fflush(stdout);
                    }
                }

                /* If this is the first time we hit an ending, latch it and start countdown */
                if (end_grace_left < 0 && why != END_NONE) {
                    end_latched = why;
                    end_grace_left = END_GRACE_TICKS;
                }

                /* If a countdown is active, tick it down each frame */
                if (end_grace_left >= 0) {
                    if (end_grace_left == 0) {
                        HudStats s;
                        compute_world_stats(&w, &s);
                        tp_note_final(&g_tp, t, (unsigned char)end_latched,
                            s.alive_pop, s.infected_pop, s.dead_pop, s.total_pop);

                        fputs(SHOW_CURSOR ANSI_RESET, stdout);
#ifdef WRAP_ON
                        fputs(WRAP_ON, stdout);
#endif
                        fflush(stdout);
                            if (end_latched == END_ZERO_INFECTED ||
                                end_latched == END_STALLED ||
                                end_latched == END_MAX_TICKS)
                            {
                                if (g_baseline_total_pop > 0) {
                                    int alive_fin = 0;
                                    int inf_fin = 0;
                                    totals_alive_infected_land(&w, &alive_fin, &inf_fin);

                                    double frac_alive = (double)alive_fin / (double)g_baseline_total_pop;
                                    if (frac_alive < ERADICATE_OVERRIDE_FRAC) {
                                        end_latched = END_ALL_DEAD;
                                    }
                                }
                            }
                        end_screen_set_save_context(&dz, &g_cfg);
                        end_screen_show_interactive(end_latched, t);

                        printf("\nSimulation complete. Press Enter to exit...");
                        getchar();
                        break;
                    }
                    else {
                        end_grace_left -= 1;
                    }
                }
            }


            /* 7) Cap FPS (simple frame pacing) */
            {
                unsigned long long dt = now_ms() - t0;
                if (dt < frame_ms) {
                    sleep_ms((unsigned)(frame_ms - dt));
                }
            }

            /* Next tick */
            t += 1;
        }
    }

    /* ----- Cleanup ----- */
    fputs(SHOW_CURSOR ANSI_RESET "\n", stdout);
    free_frame_cache();
    free_world(&w);
    tp_free(&g_tp);
    return 0;
}
