#ifndef TURNPOINTS_H
#define TURNPOINTS_H

#include <stddef.h>
#include <stdint.h>  /* uint64_t */

/*
  Turnpoints = timeline snapshots of “notable events” during the simulation.
  We record a compact linked list with the tick number, basic population stats,
  whether the cure was active at that moment, and WHY the snapshot was taken.
*/

/* Why we recorded the point */
typedef enum {
    TP_NONE = 0,
    TP_SYMPTOM = 1,          /* detail = symptom id (0..n-1) */
    TP_FIRST_INFECTED_SHIP,  /* detail unused */
    TP_INF_THRESHOLD,        /* detail = 10|20|50|80|100 (% of world infected) */
    TP_DEAD_THRESHOLD,       /* detail = 10|20|50|80|100 (% of world dead) */
    TP_CURE_ACTIVE,          /* cure flips to active this tick */
    TP_FINAL_TICK            /* final snapshot at end; detail is caller-defined */
} TurnReason;

/* Why use a 64-bit unsigned type (uint64_t) for counts?
   - Counts (alive/infected/dead/total) are never negative -> unsigned communicates intent.
   - 32-bit can overflow on large maps or long runs. 64-bit (~1.8e19) gives huge headroom
     when summing per-cell populations and cumulative deaths/infections.
   - We use uint64_t for an explicit 64-bit width (portable across compilers/OSes).
   Printing tip: use <inttypes.h> and PRIu64 (e.g., printf("%" PRIu64, v);).
   Percentages: cast to double first to avoid integer division.
*/

/* One snapshot */
typedef struct {
    int      tick;        /* simulation tick when this point was recorded */

    /* 64-bit unsigned = safe headroom for big maps / long runs */
    uint64_t alive;       /* living population at this tick */
    uint64_t infected;    /* infected population at this tick */
    uint64_t dead;        /* cumulative deaths at this tick */
    uint64_t total;       /* convenience: world total for percent math */

    unsigned char cure_active;  /* 0/1 at this moment */
    unsigned char reason;       /* TurnReason */
    unsigned char detail;       /* threshold (10/20/50/80/100) or symptom id */
} TurnPoint;

/* Linked list node (simple forward list) */
typedef struct TPNode {
    TurnPoint      p;
    struct TPNode* next;
} TPNode;

/* Collector + guards
   - We append nodes as events happen.
   - Latches ensure each milestone is recorded once (e.g., first infected ship).
   - inf_next_idx / dead_next_idx tell us the next % threshold to look for.
*/
typedef struct {
    TPNode* head;
    TPNode* tail;
    size_t  count;

    /* latches/guards so we record each milestone once */
    unsigned char saw_cure;       /* 0 until first cure_active */
    unsigned char saw_ship;       /* 0 until first infected ship */
    unsigned char inf_next_idx;   /* 0..4 → thresholds[inf_next_idx] */
    unsigned char dead_next_idx;  /* 0..4 → thresholds[dead_next_idx] */
} TurnTracker;

/* lifecycle */
void tp_init(TurnTracker* tp);
void tp_free(TurnTracker* tp);

/* Why use unsigned long long for counts?
   - These are *counts* (alive/infected/dead/total) ⇒ never negative ⇒ unsigned communicates intent.
   - 32-bit can overflow quickly on large maps or long runs. 64-bit (~1.8e19 max) gives huge headroom
     for summing per-cell populations, cumulative deaths, etc.
   - We prefer unsigned long long over size_t because size_t may be only 32-bit on some targets.
   - If you prefer an explicit 64-bit type, uint64_t is fine too (requires <stdint.h>), but we keep
     unsigned long long to avoid changing includes / public API.

   Printing tip: use %llu.  Percentages: cast to double first to avoid integer division.
*/


/* appenders for specific events (we pass raw stats so this module
   stays independent of World/Disease types) */
void tp_note_symptom(TurnTracker* tp, int tick, int symptom_id,
    uint64_t alive, uint64_t infected, uint64_t dead, uint64_t total);

void tp_note_first_infected_ship(TurnTracker* tp, int tick,
    uint64_t alive, uint64_t infected, uint64_t dead, uint64_t total);

void tp_note_cure_active(TurnTracker* tp, int tick,
    uint64_t alive, uint64_t infected, uint64_t dead, uint64_t total);

/* per-tick watcher that auto-logs INF/DEAD thresholds if crossed
   (can log multiple thresholds in a single tick if we jump far enough) */
void tp_update_thresholds(TurnTracker* tp, int tick,
    uint64_t alive, uint64_t infected, uint64_t dead, uint64_t total,
    int cure_active);

/* final snapshot when your grace window expires (detail is caller-defined) */
void tp_note_final(TurnTracker* tp, int tick, unsigned char end_detail,
    uint64_t alive, uint64_t infected, uint64_t dead, uint64_t total);

#endif /* TURNPOINTS_H */
