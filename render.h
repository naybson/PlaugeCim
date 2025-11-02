// ===================== render.h =====================
#ifndef RENDER_H
#define RENDER_H

#include "types.h"
#include "disease.h"

// ===================== Public API =====================


// Allocate memory for render cache
void alloc_frame_cache(const World* w);

// Free the render cache
void free_frame_cache(void);

// Invalidate the entire render cache (force full redraw)
void invalidate_frame_cache(const World* w);

// Set how strong the cure effect appears on screen (0.0 - 1.0)
void render_set_cure_fade(float t);

// Converts a map coordinate (cx, cy) to a screen coordinate (sx, sy)
void render_cell_to_screen(const World* w, int cx, int cy, int* sx, int* sy);

// Draw static header and population HUD summary (used early game)
void draw_header_and_hud(const World* w, int tick, const Disease* dz);

// Incrementally draw all changed tiles to the screen
void draw_frame_incremental(const World* w);

// Restore a cell from the cache (used by overlays like ships)
void restore_cell_from_cache(const World* w, int x, int y);

#endif // RENDER_H