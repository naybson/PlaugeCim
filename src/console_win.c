#include <stdio.h>
#include <string.h>

#include "console_win.h"
#include "ansi.h"
#include "hud.h"
#include "render.h"
#include "ships.h"
#include "utils.h"

#define MIN_BUFFER_EXTRA_ROWS 3

static int last_cols = -1;
static int last_rows = -1;

#ifdef _WIN32
#include <wchar.h>
#include <windows.h>

void enable_vt_mode(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
}

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

int running_in_windows_terminal(void) {
    DWORD n = GetEnvironmentVariableA("WT_SESSION", NULL, 0);
    return (n > 0);
}

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

#else
/* Linux / POSIX implementation */
#include <sys/ioctl.h>
#include <unistd.h>

void enable_vt_mode(void) { /* no-op: ANSI works in all modern Linux terminals */ }

void ensure_console_buffer_at_least(int cols, int rows) {
    (void)cols; (void)rows; /* no-op: terminal manages scrollback */
}

int running_in_windows_terminal(void) { return 1; }

int check_console_resize(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0) return 0;
    int cols = (int)w.ws_col;
    int rows = (int)w.ws_row;
    if (cols != last_cols || rows != last_rows) {
        last_cols = cols;
        last_rows = rows;
        return 1;
    }
    return 0;
}

#endif /* _WIN32 */

/* full_redraw_on_resize is platform-independent (uses ANSI + render API) */
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
