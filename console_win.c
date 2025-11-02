#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include "console_win.h"
#include "ansi.h"
#include "render.h"
#include "ships.h"
#include "utils.h"

// ========== CONSTANTS ==========
#define MIN_BUFFER_EXTRA_ROWS 3

// ========== INTERNAL STATE ==========
static int last_cols = -1;
static int last_rows = -1;

// ========== FUNCTION DEFINITIONS ==========

/*
   Enables ANSI color output in Windows console (VT Mode)
*/
void enable_vt_mode(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
}

/*
   Ensures console buffer is at least cols x rows
*/
void ensure_console_buffer_at_least(int cols, int rows) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return;

    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(h, &info)) return;

    COORD size = info.dwSize;
    if (size.X < cols) size.X = (SHORT)cols;
    if (size.Y < rows) size.Y = (SHORT)rows;

    SetConsoleScreenBufferSize(h, size);
}

/*
   Returns 1 if running inside Windows Terminal
*/
int running_in_windows_terminal(void) {
    DWORD n = GetEnvironmentVariableA("WT_SESSION", NULL, 0);
    return (n > 0);
}

/*
   Triggers full redraw on resize/zoom.
   Forces console buffer size, clears screen, redraws everything.
*/
void full_redraw_on_resize(const World* w) {
    ensure_console_buffer_at_least(w->width, w->height + MIN_BUFFER_EXTRA_ROWS);
    invalidate_frame_cache(w);
    reset_ship_overlays();

    fputs(SYNC_START, stdout);
    fputs(CLEAR_ALL, stdout);

    draw_static_header(w);
    draw_frame_incremental(w);

    fputs(SYNC_END, stdout);
    fflush(stdout);
}

/*
   Returns 1 if the console window size has changed since last check
*/
int check_console_resize(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return 0;

    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(h, &info)) return 0;

    int cols = info.srWindow.Right - info.srWindow.Left + 1;
    int rows = info.srWindow.Bottom - info.srWindow.Top + 1;

    if (cols != last_cols || rows != last_rows) {
        last_cols = cols;
        last_rows = rows;
        return 1;
    }

    return 0;
}
