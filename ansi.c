#include <stdio.h>
#include "ansi.h"

/*
   ANSI terminal helpers:
   - Color palette based on infection levels
   - Cursor movement with CUP (Cursor Position)
*/

// --- Infection colors palette (land tiles) ---
static const char* C_HEALTHY = "\x1b[38;5;255m";  // White
static const char* C_218 = "\x1b[38;5;218m";  // Light pink
static const char* C_211 = "\x1b[38;5;211m";  // Pink
static const char* C_205 = "\x1b[38;5;205m";  // Magenta
static const char* C_204 = "\x1b[38;5;204m";  // Red-magenta
static const char* C_203 = "\x1b[38;5;203m";  // Strong red-pink
static const char* C_197 = "\x1b[38;5;197m";  // Red
static const char* C_196 = "\x1b[38;5;196m";  // Deep red (max infection)

// --- Sea color (used for water tiles) ---
const char* sea_color(void) {
    return "\x1b[38;5;39m"; // Ocean blue
}

// --- Pick color based on infection bin ---
const char* color_by_bins(int i_bin, int d_bin, int is_sea) {
    (void)d_bin; // Unused for now, reserved for future death shading

    if (is_sea) return sea_color();

    if (i_bin <= 0) return C_HEALTHY;
    if (i_bin <= 1) return C_218;
    if (i_bin <= 3) return C_211;
    if (i_bin <= 4) return C_205;
    if (i_bin <= 5) return C_204;
    if (i_bin <= 7) return C_203;
    if (i_bin <= 9) return C_197;
    return C_196;  // 10+ infection bin
}

// --- Move cursor to (row, col) in terminal ---
void cup(int row, int col) {
    printf("\x1b[%d;%dH", row, col);
}
