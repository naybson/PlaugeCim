#ifndef SYMPTOMS_H
#define SYMPTOMS_H

/*
 * symptoms.h — Symptom progression tree and effect aggregation
 *
 * What this module does:
 *  - Defines a fixed symptom tree (IDs + parent/children topology).
 *  - Tracks which symptoms are active over time (bitmask).
 *  - Supports time-based “mutation” unlocks (periodic random child activation).
 *  - Computes the total additive effects of all active symptoms for a cell,
 *    including climate-specific bonuses (HOT/COLD).
 *
 * Notes:
 *  - The actual numeric tuning for each symptom’s effects lives in symptoms.c.
 *    You tweak numbers there (promoted to #defines), not here.
 *  - Awareness is tracked as an integer but may be a placeholder depending on your UI/logic.
 *  - The active_mask uses 1 bit per symptom; SYM_COUNT must stay <= 32.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "types.h"   /* for Climate */

    /* ------------------------------------------------------------------------- */
    /* Symptom IDs (tree order). Keep in sync with the static table in .c        */
    /* ------------------------------------------------------------------------- */
    typedef enum {
        SYM_START = 0,
        SYM_COUGH,
        SYM_SWEATING,
        SYM_SNEEZING,
        SYM_SKIN_RASH,
        SYM_BLISTERING,
        SYM_LUNG_FAILURE,
        SYM_FEVER,
        SYM_TOTAL_ORGAN_FAILURE,
        SYM_COUNT
    } SymptomId;

    typedef struct {
        double add_within;
        double add_neighbor;
        double add_in_hot;
        double add_in_cold;
        double add_mortality;
        int    add_awareness;
    } SymEffect;

    typedef struct SymNode {
        int id;
        const char* name;
        int parent;
        int left;
        int right;
        SymEffect fx;
    } SymNode;

    typedef struct SymRunNode {
        const SymNode* def;      
        struct SymRunNode* next;
    } SymRunNode;

    /* Runtime state (now includes both bitmask and linked list) */
    typedef struct {
        /* NEW: linked list head of active symptoms (classic p->next) */
        SymRunNode* head;

        /* OLD: kept for API compatibility and fast checks/UI helpers */
        uint32_t active_mask;  /* bit i -> symptom i active (SYM_COUNT <= 32) */
        int      awareness;    /* accumulates add_awareness from activated symptoms */

        int mutation_period;   /* ticks between mutation attempts (e.g., 150) */
        int ticks_to_next;     /* countdown until next mutation attempt */
    } SymptomState;

    /* ------------------------------------------------------------------------- */
    /* Static table access (read-only)                                            */
    /* ------------------------------------------------------------------------- */

    /**
     * symptoms_tree
     * Returns a pointer to the immutable array of SymNode (size SYM_COUNT).
     * The topology (parent/children) and per-symptom names/effects live here.
     */
    const SymNode* symptoms_tree(void);

    /**
     * symptom_name
     * Safe accessor for a symptom’s display name. Returns "Unknown" if id is out of range.
     */
    const char* symptom_name(SymptomId id);

    /* ------------------------------------------------------------------------- */
    /* Lifecycle                                                                 */
    /* ------------------------------------------------------------------------- */

    /**
     * symptoms_init
     * Initialize a SymptomState:
     *  - Clears the active_mask and sets SYM_START active.
     *  - Sets mutation period (clamped to a sane minimum inside the implementation).
     *  - Resets the countdown to the full period.
     */
    void symptoms_init(SymptomState* st, int mutation_period_ticks);

    /**
     * symptoms_set_mutation_period
     * Change the mutation cadence at runtime and reset the countdown.
     * The active set of symptoms is not changed.
     */
    void symptoms_set_mutation_period(SymptomState* st, int mutation_period_ticks);

    /* ------------------------------------------------------------------------- */
    /* Activation / Mutation                                                     */
    /* ------------------------------------------------------------------------- */

    /**
     * symptoms_is_active
     * Return 1 if the given symptom id is currently active, else 0.
     */
    int  symptoms_is_active(const SymptomState* st, SymptomId id);

    /**
     * symptoms_activate
     * Attempt to activate a symptom explicitly:
     *  - Fails if the parent is not yet active (tree gating).
     *  - Succeeds idempotently (activating an already-active symptom is a no-op).
     * Returns 1 on success, 0 on failure.
     */
    int  symptoms_activate(SymptomState* st, SymptomId id);

    /**
     * symptoms_mutation_tick
     * Decrement the internal countdown; when it reaches zero, uniformly choose one
     * “frontier” child (a direct child of any active node that is not yet active)
     * and activate it. Countdown is reset to the full period either way.
     * If a new symptom is activated, writes its id to out_new_id (if non-NULL) and returns 1.
     * Returns 0 otherwise.
     */
    int  symptoms_mutation_tick(SymptomState* st, SymptomId* out_new_id);

    /* ------------------------------------------------------------------------- */
    /* Effects combiner                                                          */
    /* ------------------------------------------------------------------------- */

    /**
     * symptoms_effect_totals
     * Sum the additive effects of all currently active symptoms for a given cell
     * climate. Climate bonuses add to BOTH within and neighbor spreads.
     *
     * Outputs (any pointer may be NULL to skip):
     *  - dW   : total delta to beta_within
     *  - dN   : total delta to beta_neighbors
     *  - dMu  : total delta to mu_mortality
     *  - dAware : total awareness contribution
     */
    void symptoms_effect_totals(const SymptomState* st, Climate climate,
        double* dW, double* dN, double* dMu, int* dAware);

    /* ------------------------------------------------------------------------- */
    /* HUD helpers                                                               */
    /* ------------------------------------------------------------------------- */

    /**
     * symptoms_ticks_to_next
     * Returns the number of ticks remaining until the next mutation attempt.
     */
    int  symptoms_ticks_to_next(const SymptomState* st);

    /**
     * symptoms_build_active_names
     * Build a list of *active* symptom names in a pre-order traversal (root first).
     * Returns the number of names written (<= max).
     */
    int  symptoms_build_active_names(const SymptomState* st, const char** out, int max);

    /**
     * symptoms_build_frontier_names
     * Build a list of “frontier” symptom names (unlockable next children).
     * Returns how many names were written (<= max).
     */
    int  symptoms_build_frontier_names(const SymptomState* st, const char** out, int max);

#endif /* SYMPTOMS_H */
