#include "turnpoints.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   /* uint64_t, UINT64_MAX */

/* ============================== Constants & Defines ============================== */

/* Threshold percentages we care about for infection/death milestones.
   Keep the order ascending; we can advance multiple per tick. */
#define TP_THRESHOLDS_COUNT   5
static const int TP_THRESHOLDS[TP_THRESHOLDS_COUNT] = { 10, 20, 50, 80, 100 };

/* Scale used when converting a fraction to percent. */
#define PERCENT_SCALE         100.0

/* Latch values for clarity (instead of hard-coded 0/1 magic). */
#define LATCH_CLEAR           0
#define LATCH_SET             1

/* ============================== Safe Integer Helpers ============================= */
/* Saturating add: returns UINT64_MAX on overflow (never wraps). */
static uint64_t u64_add_sat(uint64_t a, uint64_t b) {
    if (UINT64_MAX - a < b) {
        return UINT64_MAX;
    }
    return a + b;
}

/* Saturating subtract: floors at 0 (never underflows). */
static uint64_t u64_sub_sat(uint64_t a, uint64_t b) {
    if (a < b) {
        return 0;
    }
    return a - b;
}

/* ============================== Internal helpers =============================== */
/* Allocate a node and append it to the tracker’s tail (O(1)). */
static void tp_append(TurnTracker* tp, const TurnPoint* src)
{
    if (tp == NULL || src == NULL) {
        return;
    }

    TPNode* n = (TPNode*)malloc(sizeof(TPNode));
    if (n == NULL) {
        /* Out of memory: fail silently; telemetry is non-critical. */
        return;
    }

    n->p = *src;
    n->next = NULL;

    if (tp->tail == NULL) {
        tp->head = n;
        tp->tail = n;
    }
    else {
        tp->tail->next = n;
        tp->tail = n;
    }
    tp->count += 1;
}

/* Fill a TurnPoint from raw values. Kept simple & explicit. */
static void tp_make_point(TurnPoint* out, int tick, TurnReason why, int detail,
    uint64_t alive, uint64_t infected, uint64_t dead, uint64_t total,
    int cure_active)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));

    out->tick = tick;
    out->alive = alive;
    out->infected = infected;
    out->dead = dead;
    out->total = total;

    if (cure_active != 0) {
        out->cure_active = (unsigned char)LATCH_SET;
    }
    else {
        out->cure_active = (unsigned char)LATCH_CLEAR;
    }

    out->reason = (unsigned char)why;
    out->detail = (unsigned char)(detail & 0xFF); /* stays byte-sized, like original */
}

/* ============================== Public API ===================================== */

void tp_init(TurnTracker* tp)
{
    if (tp == NULL) {
        return;
    }
    memset(tp, 0, sizeof(*tp));

    tp->head = NULL;
    tp->tail = NULL;
    tp->count = 0;

    tp->saw_cure = LATCH_CLEAR;
    tp->saw_ship = LATCH_CLEAR;
    tp->inf_next_idx = 0; /* next infection threshold index to log */
    tp->dead_next_idx = 0; /* next death threshold index to log */
}

void tp_free(TurnTracker* tp)
{
    if (tp == NULL) {
        return;
    }

    TPNode* it = tp->head;
    while (it != NULL) {
        TPNode* nxt = it->next;
        free(it);
        it = nxt;
    }

    memset(tp, 0, sizeof(*tp));
}

/* Note: all note_* functions take the full snapshot so this module
   stays independent from World/Disease structures. */

void tp_note_symptom(TurnTracker* tp, int tick, int symptom_id,
    uint64_t alive, uint64_t infected, uint64_t dead, uint64_t total)
{
    TurnPoint p;
    tp_make_point(&p, tick, TP_SYMPTOM, symptom_id, alive, infected, dead, total, 0);
    tp_append(tp, &p);
}

void tp_note_first_infected_ship(TurnTracker* tp, int tick,
    uint64_t alive, uint64_t infected, uint64_t dead, uint64_t total)
{
    if (tp == NULL) {
        return;
    }
    if (tp->saw_ship == LATCH_SET) {
        return; /* already recorded */
    }

    tp->saw_ship = LATCH_SET;

    TurnPoint p;
    tp_make_point(&p, tick, TP_FIRST_INFECTED_SHIP, 0, alive, infected, dead, total, 0);
    tp_append(tp, &p);
}

void tp_note_cure_active(TurnTracker* tp, int tick,
    uint64_t alive, uint64_t infected, uint64_t dead, uint64_t total)
{
    if (tp == NULL) {
        return;
    }
    if (tp->saw_cure == LATCH_SET) {
        return; /* already recorded */
    }

    tp->saw_cure = LATCH_SET;

    TurnPoint p;
    tp_make_point(&p, tick, TP_CURE_ACTIVE, 0, alive, infected, dead, total, 1);
    tp_append(tp, &p);
}

/* thresholds we care about (percent of world) */
void tp_update_thresholds(TurnTracker* tp, int tick,
    uint64_t alive, uint64_t infected, uint64_t dead, uint64_t total,
    int cure_active)
{
    if (tp == NULL) {
        return;
    }
    if (total == 0ULL) {
        return; /* avoid division by zero; also nothing to measure */
    }

    /* Compute percentages in double for safety.
       Infection % of WORLD (not of alive-only) matches original intent.
       If alive == 0, we explicitly define infected% := 0 to avoid weird ratios. */
    double inf_pct = 0.0;
    double dead_pct = 0.0;

    if (alive > 0ULL) {
        inf_pct = PERCENT_SCALE * (double)infected / (double)total;
    }
    else {
        inf_pct = 0.0;
    }
    dead_pct = PERCENT_SCALE * (double)dead / (double)total;

    /* Infection thresholds: can advance multiple steps in one tick.
       Example: jump from 9% to 55% => we should log both 10% and 50%. */
    while (tp->inf_next_idx < (unsigned char)TP_THRESHOLDS_COUNT) {
        int need = TP_THRESHOLDS[tp->inf_next_idx];
        if (inf_pct >= (double)need) {
            TurnPoint p;
            tp_make_point(&p, tick, TP_INF_THRESHOLD, need, alive, infected, dead, total, cure_active);
            tp_append(tp, &p);
            tp->inf_next_idx += 1;
        }
        else {
            break;
        }
    }

    /* Death thresholds: same multi-step logic. */
    while (tp->dead_next_idx < (unsigned char)TP_THRESHOLDS_COUNT) {
        int need = TP_THRESHOLDS[tp->dead_next_idx];
        if (dead_pct >= (double)need) {
            TurnPoint p;
            tp_make_point(&p, tick, TP_DEAD_THRESHOLD, need, alive, infected, dead, total, cure_active);
            tp_append(tp, &p);
            tp->dead_next_idx += 1;
        }
        else {
            break;
        }
    }
}

void tp_note_final(TurnTracker* tp, int tick, unsigned char end_detail,
    uint64_t alive, uint64_t infected, uint64_t dead, uint64_t total)
{
    TurnPoint p;
    tp_make_point(&p, tick, TP_FINAL_TICK, (int)end_detail, alive, infected, dead, total, 0);
    tp_append(tp, &p);
}
