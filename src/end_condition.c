#include "end_condition.h"
#include "disease.h"
#include "config.h"
#include <stdio.h>

/* Treat tiny fluctuations as “flat”; and ignore stall before early-game */
#define EC_INF_TOL            5     /* people */
#define EC_MIN_RUNTIME_TICKS  300   /* ticks */

/* ===================== Local constants ===================== */

/* Default headline if something weird leaks through the cracks. */
#define EC_DEFAULT_TITLE   "Simulation Finished"

/* ===================== Optional cure hook ===================== */
/*
  Later: wire your real cure flag here (e.g., dz->cure_found).
  For now it always returns 0 and is NOT used by endcondition_update(); we
  let the game flip recovery mode explicitly via endcondition_set_recovery().
*/
static int cure_is_achieved(const struct Disease* dz) {
    (void)dz; /* unused for now */
    return 0;
}

/* ===================== Public API ===================== */
/* Set initial values and thresholds.
   Nothing “clever” here: we just store your knobs and reset counters. */
void endcondition_init(EndConditionTracker* t, int plateau_limit, int max_ticks) {
    if (t == NULL) {
        return;
    }

    t->plateau_ticks = 0;
    t->plateau_limit = plateau_limit;
    t->last_infected = -1;

    t->max_ticks = max_ticks;

    t->in_recovery = 0;
    t->zero_ticks = 0;
    t->zero_limit = EC_MIN_ZERO_LIMIT;

    t->soft_stall_hint = 0;                    /* NEW */
    t->min_runtime_ticks = EC_MIN_RUNTIME_TICKS; /* NEW */
}

/* Enter/leave recovery mode (post-cure rules).
   - enabled != 0 turns it on; 0 turns it off.
   - zero_limit must be >= EC_MIN_ZERO_LIMIT, otherwise we clamp to that minimum.
   We do NOT change other fields (so you can enable/disable without nuking history,
   except we reset zero_ticks to count a fresh “consecutive” run). */
void endcondition_set_recovery(EndConditionTracker* t, int enabled, int zero_limit) {
    if (t == NULL) {
        return;
    }

    if (enabled != 0) {
        t->in_recovery = 1;
    }
    else {
        t->in_recovery = 0;
    }

    if (zero_limit >= EC_MIN_ZERO_LIMIT) {
        t->zero_limit = zero_limit;
    }
    else {
        t->zero_limit = EC_MIN_ZERO_LIMIT;
    }

    t->zero_ticks = 0; /* start counting fresh */
}

/*
  Called once per tick with the current “alive” and “infected” counts
  and the current tick number.

  Decision tree (in this order):
    1) Hard cap: if max_ticks > 0 and tick >= max_ticks => END_MAX_TICKS.
    2) If everyone’s dead => END_ALL_DEAD.
    3) If in recovery mode (post-cure):
         - Count consecutive zero-infected ticks; when we reach zero_limit => END_CURE.
         - Otherwise keep going (stall is ignored in recovery).
    4) Pre-cure (normal mode):
         - If infected == 0 => END_ZERO_INFECTED.
         - Otherwise track “plateau” (exactly equal to last infected).
           If we stay flat for plateau_limit ticks => END_STALLED.

  Subtle point: “stall” means *exact equality* of the infected count. That matches the
  original logic and can be important if you want to catch truly frozen dynamics.
*/
EndReason endcondition_update(EndConditionTracker* t,int alive, int infected,const struct Disease* dz,int tick)
{
    (void)dz; /* placeholder for the future: cure tie-ins or other disease flags */

    if (t == NULL) {
        return END_NONE;
    }

    /* 1) Hard cap applies in all phases. */
    if (t->max_ticks > 0) {
        if (tick >= t->max_ticks) {
            return END_MAX_TICKS;
        }
    }

    /* 2) Everyone dead: always terminal. */
    if (alive <= 0.001f) {
        return END_ALL_DEAD;
    }

    /* 3) Recovery mode: wait for sustained zero-infected. */
    if (t->in_recovery == 1) {
        if (infected <= 0) {
            t->zero_ticks += 1;

            if (t->zero_ticks >= t->zero_limit) {
                /* “World saved” path after cure. */
                return END_CURE;
            }
        }
        else {
            /* Not at zero anymore: reset the consecutive counter. */
            t->zero_ticks = 0;
        }
        return END_NONE;
    }

    /* 4) Pre-cure rules. */
    if (infected <= 0) {
        return END_ZERO_INFECTED;
    }

    /* On the very first tick we see a positive infected value,
       initialize plateau tracking (but do not declare a stall yet). */
    if (t->last_infected < 0) {
        t->last_infected = infected;
        t->plateau_ticks = 0;
        return END_NONE;
    }

    /* Plateau bookkeeping: identical infected => +1; change => reset & store. */
    int delta = infected - t->last_infected;
    if (delta < 0) {
        delta = -delta;
    }

    if (delta <= EC_INF_TOL) {
        t->plateau_ticks += 1;
    }
    else {
        t->plateau_ticks = 0;
        t->last_infected = infected;
    }

    /* stall condition */
    if (t->plateau_limit > 0) {
        if (tick >= t->min_runtime_ticks) {
            if (alive > 0) {
                if (infected > 0) {
                    if (infected < alive) {
                        if (t->plateau_ticks >= t->plateau_limit) {

                            int fully_infected = 0;
                            if (infected >= alive) {
                                fully_infected = 1;
                            }

                            int mutations_on = 0;
                            if (dz != NULL) {
                                if (dz->mutations_enabled != 0) {
                                    mutations_on = 1;
                                }
                            }

                            /* If fully infected and mutations are on, ignore stall. */
                            if (fully_infected == 1 && mutations_on == 1) {
                                t->last_infected = infected;
                                t->plateau_ticks = 0;
                                return END_NONE;
                            }

                            /* Otherwise: raise a soft hint and keep running. */
                            t->soft_stall_hint = 1;
                            t->last_infected = infected;  /* avoid instant re-trigger */
                            t->plateau_ticks = 0;
                            return END_NONE;
                        }
                    }
                }
            }
        }
    }


    return END_NONE;
}

/* ===================== Titles for end screen ===================== */
/* Centralized mapping: guarantees all screens use the same phrasing. */
const char* endreason_title(EndReason r)
{
    const char* title = EC_DEFAULT_TITLE; /* safe default */

    switch (r) {
    case END_NONE:
        title = EC_DEFAULT_TITLE;
        break;

    case END_ZERO_INFECTED:
        title = "Infection Eliminated";
        break;

    case END_ALL_DEAD:
        title = "World Population Eradicated";
        break;

    case END_STALLED:
        title = "Spread Stalled";
        break;

    case END_CURE:
        title = "Cure Achieved";
        break;

    case END_MAX_TICKS:
        title = "Tick Limit Reached";
        break;

    default:
        /* leave default */
        break;
    }

    return title;
}
