#ifndef ANSI_H
#define ANSI_H

// Basic ANSI escape definitions
#define ESC             "\x1b"          // Escape character
#define CSI             ESC "["        // Control Sequence Introducer
#define ANSI_RESET      CSI "0m"       // Reset all attributes

// Cursor and screen control
#define HIDE_CURSOR     CSI "?25l"
#define SHOW_CURSOR     CSI "?25h"
#define WRAP_OFF        CSI "?7l"
#define WRAP_ON         CSI "?7h"
#define ERASE_LINE      CSI "2K"
#define ALT_SCREEN_ON   CSI "?1049h"   // Use alternate screen buffer
#define ALT_SCREEN_OFF  CSI "?1049l"
#define CLEAR_ALL       CSI "3J" CSI "H" CSI "2J" // Clear scrollback + screen

// Synchronized terminal drawing (optional)
#define SYNC_START      CSI "?2026h"
#define SYNC_END        CSI "?2026l"

// --- Color Functions ---
/**
 * Returns the ANSI escape string for sea color (used for water tiles).
 */
const char* sea_color(void);

/**
 * Returns an ANSI color string based on infection and death bins.
 * @param i_bin - infection intensity bin (0–10+)
 * @param d_bin - death bin (ignored for now)
 * @param is_sea - 1 if sea tile, 0 if land
 */
const char* color_by_bins(int i_bin, int d_bin, int is_sea);

/**
 * Moves the terminal cursor to (row, col) (1-based coordinates).
 */
void cup(int row, int col);

#endif /* ANSI_H */
