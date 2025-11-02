#ifndef CONSOLE_WIN_H
#define CONSOLE_WIN_H

#include "types.h"

/*
   Windows Console Utility Header
   Functions to handle:
   - Enabling ANSI support
   - Resizing console buffer
   - Detecting window resizing
   - Redrawing full screen after zoom
*/

// Enables ANSI VT mode for color output
void enable_vt_mode(void);

// Ensures the console buffer is at least the given size
void ensure_console_buffer_at_least(int cols, int rows);

// Returns 1 if running in Windows Terminal
int running_in_windows_terminal(void);

// Triggers a full redraw on resize/zoom
void full_redraw_on_resize(const World* w);

// Returns 1 if console was resized since last check
int check_console_resize(void);

#endif /* CONSOLE_WIN_H */
