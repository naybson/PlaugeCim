// ===================== symptoms.c (drop-in) =====================
// Purpose:
//   Manage the symptom progression tree and compute additive effects.
//   This version keeps the bitmask (fast checks, existing UI),
//   and ALSO maintains a classic linked list of active symptoms.
//   Activation performs a sorted insert by mortality (least -> most fatal)
//   so the HUD can print the list in that order without extra work.
//
// Design highlights:
//   - Static tree N[] encodes parent/children and per-symptom effects.
//   - Runtime state uses both a bitmask and a linked list of active nodes.
//   - Timer-based mutation engine: collect frontier -> pick -> activate.
//   - Effects are derived by walking the linked list (simple & fast).
//
// Invariants:
//   - SYM_START is always active.
//   - A symptom can activate only if its parent is active.
//   - Frontier = children of any active node that aren't active.
//
// ----------------------------------------------------------------

#include <stdlib.h>   /* malloc, free */
#include <string.h>   /* memset */
#include <math.h>     /* floor */

#include "symptoms.h"
#include "rng.h"      /* rng01() -> double [0,1) */

// ===================== Tuning Knobs (Defines) =====================
// Global behavior knobs
#define SYM_MIN_MUT_PERIOD_TICKS   1   /* minimum allowed ticks between mutations */
#define SYM_AWARENESS_INITIAL      0   /* starting awareness value */

// --- Symptom effect constants (easy to tweak) ---
// Format: WITHIN, NEIGHBOR, HOT_BONUS, COLD_BONUS, MORTALITY, AWARENESS

// Root / Start (gentle opener)
#define FX_START_WITHIN       0.000
#define FX_START_NEIGHBOR     0.000
#define FX_START_HOT          0.000
#define FX_START_COLD         0.000
#define FX_START_MORTALITY    0.0000
#define FX_START_AWARENESS    0

// Early: more spread, little/no death
#define FX_COUGH_WITHIN       0.004   /* neighbor-biased spread boost */
#define FX_COUGH_NEIGHBOR     0.006
#define FX_COUGH_HOT          0.000
#define FX_COUGH_COLD         0.001
#define FX_COUGH_MORTALITY    0.0000
#define FX_COUGH_AWARENESS    1

#define FX_SWEAT_WITHIN       0.003   /* stronger in HOT */
#define FX_SWEAT_NEIGHBOR     0.001
#define FX_SWEAT_HOT          0.002
#define FX_SWEAT_COLD         0.000
#define FX_SWEAT_MORTALITY    0.0000
#define FX_SWEAT_AWARENESS    0

#define FX_SNEEZE_WITHIN      0.004   /* cold seasons pop more */
#define FX_SNEEZE_NEIGHBOR    0.006
#define FX_SNEEZE_HOT         0.000
#define FX_SNEEZE_COLD        0.002
#define FX_SNEEZE_MORTALITY   0.0003
#define FX_SNEEZE_AWARENESS   2

#define FX_RASH_WITHIN        0.002
#define FX_RASH_NEIGHBOR      0.001
#define FX_RASH_HOT           0.001
#define FX_RASH_COLD          0.000
#define FX_RASH_MORTALITY     0.0000
#define FX_RASH_AWARENESS     2

// Mid: mortality starts to matter
#define FX_LUNG_WITHIN        0.006
#define FX_LUNG_NEIGHBOR      0.003
#define FX_LUNG_HOT           0.000
#define FX_LUNG_COLD          0.000
#define FX_LUNG_MORTALITY     0.0060
#define FX_LUNG_AWARENESS     3

#define FX_FEVER_WITHIN       0.001
#define FX_FEVER_NEIGHBOR     0.001
#define FX_FEVER_HOT          0.000
#define FX_FEVER_COLD         0.000
#define FX_FEVER_MORTALITY    0.0040
#define FX_FEVER_AWARENESS    2

// Late: big mortality spike, little extra spread
#define FX_TOF_WITHIN         0.000
#define FX_TOF_NEIGHBOR       0.000
#define FX_TOF_HOT            0.000
#define FX_TOF_COLD           0.000
#define FX_TOF_MORTALITY      0.0250
#define FX_TOF_AWARENESS      0

// New dermal mid node after Skin Rash
#define FX_BLISTER_WITHIN     0.003
#define FX_BLISTER_NEIGHBOR   0.001
#define FX_BLISTER_HOT        0.001
#define FX_BLISTER_COLD       0.000
#define FX_BLISTER_MORTALITY  0.0010
#define FX_BLISTER_AWARENESS  3

// ===================== Static Symptom Tree =====================
// id, name, parent, left child, right child, effects
static const SymNode N[SYM_COUNT] = {
    { SYM_START, "", -1, SYM_COUGH, SYM_SWEATING,
      { FX_START_WITHIN, FX_START_NEIGHBOR, FX_START_HOT,  FX_START_COLD,  FX_START_MORTALITY, FX_START_AWARENESS } },

      // Early
      { SYM_COUGH,"|Cough|", SYM_START, SYM_SNEEZING, -1,
        { FX_COUGH_WITHIN, FX_COUGH_NEIGHBOR, FX_COUGH_HOT,  FX_COUGH_COLD,  FX_COUGH_MORTALITY, FX_COUGH_AWARENESS } },

      { SYM_SWEATING,"|Sweating|", SYM_START, SYM_SKIN_RASH, -1,
        { FX_SWEAT_WITHIN, FX_SWEAT_NEIGHBOR, FX_SWEAT_HOT,  FX_SWEAT_COLD,  FX_SWEAT_MORTALITY, FX_SWEAT_AWARENESS } },

      { SYM_SNEEZING,"|Sneezing|", SYM_COUGH, SYM_LUNG_FAILURE, SYM_FEVER,
        { FX_SNEEZE_WITHIN, FX_SNEEZE_NEIGHBOR, FX_SNEEZE_HOT, FX_SNEEZE_COLD, FX_SNEEZE_MORTALITY, FX_SNEEZE_AWARENESS } },

        // Skin Rash → Blistering
        { SYM_SKIN_RASH, "|Skin Rash|", SYM_SWEATING, SYM_BLISTERING, -1,
          { FX_RASH_WITHIN, FX_RASH_NEIGHBOR, FX_RASH_HOT, FX_RASH_COLD, FX_RASH_MORTALITY, FX_RASH_AWARENESS } },

          // Blistering → TOF (second path)
          { SYM_BLISTERING, "|!Blistering!|", SYM_SKIN_RASH, SYM_TOTAL_ORGAN_FAILURE, -1,
            { FX_BLISTER_WITHIN, FX_BLISTER_NEIGHBOR, FX_BLISTER_HOT, FX_BLISTER_COLD,
              FX_BLISTER_MORTALITY, FX_BLISTER_AWARENESS } },

              // Mid
              { SYM_LUNG_FAILURE, "|!Lung Failure!|", SYM_SNEEZING, -1, -1,
                { FX_LUNG_WITHIN, FX_LUNG_NEIGHBOR, FX_LUNG_HOT, FX_LUNG_COLD, FX_LUNG_MORTALITY, FX_LUNG_AWARENESS } },

              { SYM_FEVER,"|!Fever!|", SYM_SNEEZING, SYM_TOTAL_ORGAN_FAILURE, -1,
                { FX_FEVER_WITHIN, FX_FEVER_NEIGHBOR, FX_FEVER_HOT,  FX_FEVER_COLD,  FX_FEVER_MORTALITY,  FX_FEVER_AWARENESS } },

                // Late
                { SYM_TOTAL_ORGAN_FAILURE, "|!!Total Organ Failure!!|", SYM_FEVER, -1, -1,
                  { FX_TOF_WITHIN, FX_TOF_NEIGHBOR, FX_TOF_HOT, FX_TOF_COLD, FX_TOF_MORTALITY, FX_TOF_AWARENESS } },
};

// Public read access to the static table
const SymNode* symptoms_tree(void) { return N; }

const char* symptom_name(SymptomId id) {
    if (id < 0 || id >= SYM_COUNT) return "Unknown";
    return N[id].name;
}

// ===================== Bit Helpers (active mask) =====================
static int bit_is_set(uint32_t m, int bit) {
    uint32_t mask = (uint32_t)1u << bit;
    return (m & mask) != 0u;
}
static void bit_set(uint32_t* m, int bit) {
    uint32_t mask = (uint32_t)1u << bit;
    *m = *m | mask;
}

// ===================== Linked-list helpers =====================

/* Compute stable "id" for a SymNode pointer.
   We avoid relying on SymNode having an explicit id field.
   Assumes all runtime 'def' pointers reference elements inside N[]. */
static int node_id_from_ptr(const SymNode* def) {
    if (def == NULL) return -1;
    return (int)(def - &N[0]); /* pointer arithmetic: index in N[] */
}

/* Insert new active node so that st->head stays sorted by mortality (asc).
   Ties are broken by node id for determinism. */
static void ll_insert_sorted_by_mortality(SymptomState* st, const SymNode* def) {
    SymRunNode* node = (SymRunNode*)malloc(sizeof(SymRunNode));
    if (node == NULL) {
        return; /* OOM: we still keep bit state so the sim continues */
    }

    node->def = def;
    node->next = NULL;

    double m_new = def->fx.add_mortality;
    int id_new = node_id_from_ptr(def);

    /* Empty list or should become new head */
    if (st->head == NULL) {
        st->head = node;
        return;
    }

    double m_head = st->head->def->fx.add_mortality;
    int id_head = node_id_from_ptr(st->head->def);

    if (m_new < m_head) {
        node->next = st->head;
        st->head = node;
        return;
    }

    if (m_new == m_head && id_new < id_head) {
        node->next = st->head;
        st->head = node;
        return;
    }

    /* Find insertion point: stop before a node with strictly greater mortality,
       or with equal mortality but higher id (tie-break). */
    SymRunNode* p = st->head;
    while (p->next != NULL) {
        double m_next = p->next->def->fx.add_mortality;
        int id_next = node_id_from_ptr(p->next->def);

        if (m_new < m_next) {
            break;
        }

        if (m_new == m_next && id_new < id_next) {
            break;
        }

        p = p->next;
    }

    node->next = p->next;
    p->next = node;
}

// ===================== Lifecycle =====================
void symptoms_init(SymptomState* st, int mutation_period_ticks)
{
    if (st == NULL) return;

    st->head = NULL;
    st->active_mask = 0u;
    st->awareness = SYM_AWARENESS_INITIAL;

    /* Root is always active: set bit + insert into sorted list (mortality=0). */
    bit_set(&st->active_mask, SYM_START);
    ll_insert_sorted_by_mortality(st, &N[SYM_START]);

    if (mutation_period_ticks < SYM_MIN_MUT_PERIOD_TICKS) {
        mutation_period_ticks = SYM_MIN_MUT_PERIOD_TICKS;
    }
    st->mutation_period = mutation_period_ticks;
    st->ticks_to_next = st->mutation_period;
}

void symptoms_set_mutation_period(SymptomState* st, int mutation_period_ticks)
{
    if (st == NULL) return;
    if (mutation_period_ticks < SYM_MIN_MUT_PERIOD_TICKS) {
        mutation_period_ticks = SYM_MIN_MUT_PERIOD_TICKS;
    }
    st->mutation_period = mutation_period_ticks;
    st->ticks_to_next = st->mutation_period;
}

// ===================== Queries =====================
int symptoms_is_active(const SymptomState* st, SymptomId id)
{
    if (st == NULL || id < 0 || id >= SYM_COUNT) return 0;
    return bit_is_set(st->active_mask, id);
}

// ===================== Activation / Mutation =====================
// Try to activate a symptom (requires parent already active).
// Returns 1 if activated, 0 otherwise.
int symptoms_activate(SymptomState* st, SymptomId id)
{
    if (st == NULL) return 0;
    if (id < 0 || id >= SYM_COUNT) return 0;

    int parent = N[id].parent;
    if (parent >= 0 && !bit_is_set(st->active_mask, parent)) {
        return 0; /* parent not unlocked yet */
    }

    if (!bit_is_set(st->active_mask, id)) {
        /* Mark bit exactly as before */
        bit_set(&st->active_mask, id);

        /* Insert into linked list with mortality sort (new behavior) */
        ll_insert_sorted_by_mortality(st, &N[id]);

        /* Awareness bump as before */
        st->awareness += N[id].fx.add_awareness;
    }
    return 1;
}

// Internal: collect frontier = children of any active node that are not active yet.
// We scan the whole table; single-parent tree guarantees no duplicates.
static int collect_frontier(const SymptomState* st, SymptomId* out, int max)
{
    int count = 0;
    for (int i = 0; i < SYM_COUNT; ++i) {
        if (bit_is_set(st->active_mask, i)) {
            int L = N[i].left;
            int R = N[i].right;

            if (L >= 0 && !bit_is_set(st->active_mask, L)) {
                if (count < max) {
                    out[count] = (SymptomId)L;
                    count = count + 1;
                }
            }

            if (R >= 0 && !bit_is_set(st->active_mask, R)) {
                if (count < max) {
                    out[count] = (SymptomId)R;
                    count = count + 1;
                }
            }
        }
    }
    return count;
}

// Advance timer; on elapsed, pick one frontier child and activate it.
// Returns 1 if a new symptom was activated.
int symptoms_mutation_tick(SymptomState* st, SymptomId* out_new_id)
{
    if (st == NULL) return 0;
    if (st->mutation_period <= 0) return 0;

    st->ticks_to_next = st->ticks_to_next - 1;
    if (st->ticks_to_next > 0) return 0;

    /* reset timer; attempt mutation */
    st->ticks_to_next = st->mutation_period;

    SymptomId cand[SYM_COUNT];
    int n = collect_frontier(st, cand, SYM_COUNT);
    if (n <= 0) return 0;

    /* uniform pick in [0, n-1], guard idx==n on r==1.0 edge */
    double r = rng01();
    int idx = (int)floor(r * (double)n);
    if (idx >= n) {
        idx = n - 1;
    }

    SymptomId chosen = cand[idx];
    if (symptoms_activate(st, chosen)) {
        if (out_new_id) {
            *out_new_id = chosen;
        }
        return 1;
    }
    return 0;
}

// ===================== Effects Combiner =====================
// Sum effects of all active symptoms for a given climate.
// We walk the linked list rather than scanning N[] and checking bits.
void symptoms_effect_totals(const SymptomState* st, Climate climate,
    double* dW, double* dN, double* dMu, int* dAware)
{
    double w = 0.0, n = 0.0, mu = 0.0;
    int aw = 0;

    if (st != NULL) {
        for (SymRunNode* p = st->head; p != NULL; p = p->next) {
            const SymEffect* fx = &p->def->fx;

            w += fx->add_within;
            n += fx->add_neighbor;
            mu += fx->add_mortality;

            if (climate == CLIMATE_HOT) {
                w += fx->add_in_hot;
                n += fx->add_in_hot;
            }
            if (climate == CLIMATE_COLD) {
                w += fx->add_in_cold;
                n += fx->add_in_cold;
            }

            aw += fx->add_awareness;
        }
    }

    if (dW) *dW = w;
    if (dN) *dN = n;
    if (dMu) *dMu = mu;
    if (dAware) *dAware = aw;
}

// ===================== HUD / Utility =====================
int symptoms_ticks_to_next(const SymptomState* st)
{
    if (st == NULL) return 0;
    return st->ticks_to_next;
}

// Pre-order traversal names of active symptoms (root before children).
// Uses the static tree structure, with active check via bitmask.
// (Kept for callers that prefer tree order; HUD can instead walk st->head.)
int symptoms_build_active_names(const SymptomState* st, const char** out, int max)
{
    if (!st || !out || max <= 0) return 0;

    int count = 0;

    /* Manual stack for pre-order */
    int stack[SYM_COUNT];
    int top = 0;
    stack[top] = SYM_START;
    top = top + 1;

    while (top > 0) {
        top = top - 1;
        int i = stack[top];

        if (i < 0) continue;
        if (bit_is_set(st->active_mask, i)) {
            if (count < max) {
                out[count] = N[i].name;
                count = count + 1;
            }
            /* Push right first so left is processed first (LIFO) */
            if (N[i].right >= 0) {
                stack[top] = N[i].right;
                top = top + 1;
            }
            if (N[i].left >= 0) {
                stack[top] = N[i].left;
                top = top + 1;
            }
        }
    }
    return count;
}

// Build a list of frontier symptom names (uniform order), using the same frontier scan.
int symptoms_build_frontier_names(const SymptomState* st, const char** out, int max)
{
    if (!st || !out || max <= 0) return 0;

    SymptomId cand[SYM_COUNT];
    int n = collect_frontier(st, cand, SYM_COUNT);

    int wrote = 0;
    for (int i = 0; i < n && wrote < max; ++i) {
        out[wrote] = N[cand[i]].name;
        wrote = wrote + 1;
    }
    return wrote;
}

// Optional cleanup if you re-init/free at runtime.
void symptoms_free(SymptomState* st)
{
    if (!st) return;

    SymRunNode* p = st->head;
    while (p) {
        SymRunNode* nxt = p->next;
        free(p);
        p = nxt;
    }

    st->head = NULL;
    st->active_mask = 0u;
    st->awareness = 0;
    st->ticks_to_next = 0;
    st->mutation_period = 0;
}
