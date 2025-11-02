#ifndef END_CONDITION_H
#define END_CONDITION_H

/*
  End-condition module
  --------------------
  Decides *why* and *when* the simulation stops.

  Two phases the tracker can be in:
    1) Normal (pre-cure): stop if infections hit zero, everyone dies,
       we stall (no change in infected for N ticks), or a hard tick cap hits.
    2) Recovery (post-cure): once cure is active, we ignore “stall” and only
       stop after seeing zero infected for K consecutive ticks.

  Notes:
    - Logic is intentionally simple and deterministic.
    - This header contains small “knobs” (defines) so you can tweak behavior
      without hunting through function bodies.
*/

    /* ===================== Tunable constants ===================== */

    /* Minimum allowed value for the “K consecutive zero-infected ticks” in recovery. */
#define EC_MIN_ZERO_LIMIT   1

/* ===================== Stop reasons ===================== */

    typedef enum EndReason {
        END_NONE = 0,         /* keep running                      */
        END_ZERO_INFECTED,    /* infected == 0 (pre-cure phase)    */
        END_ALL_DEAD,         /* alive  == 0                       */
        END_STALLED,          /* infected didn’t change N ticks    */
        END_CURE,             /* recovery: zero infected sustained */
        END_MAX_TICKS         /* safety cap reached                */
    } EndReason;

    /* ===================== Small state tracker ===================== */
    /* Keep this on the simulation side; update it once per tick. */
    typedef struct EndConditionTracker {
        /* Stall detection (pre-cure only) */
        int plateau_ticks;    /* how many consecutive ticks infected == last_infected */
        int plateau_limit;    /* how many plateau ticks count as “stalled”           */
        int last_infected;    /* previous infected value (or <0 if uninitialized)    */

        /* Global hard cap on run length */
        int max_ticks;        /* if >0 and tick >= max_ticks => END_MAX_TICKS        */

        /* Recovery phase (post-cure only) */
        int in_recovery;      /* 1 once cure activates; changes end condition rules  */
        int zero_ticks;       /* consecutive ticks with infected == 0                */
        int zero_limit;       /* how many consecutive zero ticks to declare END_CURE */

        int soft_stall_hint;    /* 0/1: suggest stopping; main loop may prompt the user */
        int min_runtime_ticks;  /* don’t consider stall before this tick (e.g., 300) */
    } EndConditionTracker;

    /* ===================== API ===================== */

    /* Initialize the tracker with your stall window and hard tick cap. */
    void endcondition_init(EndConditionTracker* t, int plateau_limit, int max_ticks);

    /* Toggle recovery mode (post-cure). While enabled, only the “zero infected for
       zero_limit consecutive ticks” rule can end the sim. */
    void endcondition_set_recovery(EndConditionTracker* t, int enabled, int zero_limit);

    /* Feed current world snapshot each tick; get END_* or END_NONE if continuing. */
    struct Disease; /* forward decl; not needed for this module’s logic yet */
    EndReason endcondition_update(EndConditionTracker* t,
        int alive,
        int infected,
        const struct Disease* dz,
        int tick);

    /* Human-readable headline for the end screen (single source of truth). */
    const char* endreason_title(EndReason r);

#endif /* END_CONDITION_H */
