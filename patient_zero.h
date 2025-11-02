/* patient_zero.h — public API for the Patient Zero picker */
#ifndef PATIENT_ZERO_H
#define PATIENT_ZERO_H

/*
 * Public entry point for selecting the starting infection cell.
 *
 * pick_patient_zero(w, dz):
 *  - Opens a modal picker that draws an overlay cursor on the map.
 *  - Returns 1 if the user confirmed a legal start (we infect 1 person there).
 *  - Returns 0 if the user pressed ESC (no changes made).
 *
 * Helpers:
 *  - p0_cell_is_valid_start: check if a given (x,y) can be used as a start.
 *  - world_seed_patient_zero: perform the actual infection at (x,y).
 *
 * Implementation detail:
 *  The overlay uses render_cell_to_screen(...) to match the renderer’s
 *  current layout—no duplicated map layout constants live here.
 */

#include "world.h"
#include "disease.h"

	/* Modal picker. Returns 1 if seeded, 0 on cancel (ESC). */
	int pick_patient_zero(World* w, Disease* dz);

	/* Validation helper: Land or Port AND population > 0. */
	int p0_cell_is_valid_start(const World* w, int x, int y);

	/* Seed helper: infect 1 person at (x,y). See .c for notes about 'total'. */
	void world_seed_patient_zero(World* w, Disease* dz, int x, int y);


#endif /* PATIENT_ZERO_H */
