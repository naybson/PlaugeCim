#ifndef _WIN32
#include "platform.h"
#include <fcntl.h>
#include <sys/select.h>
#include <stdlib.h>

/* ── State ──────────────────────────────────────────────────────────────── */
static int            s_raw_on  = 0;
static struct termios s_saved;
static int            s_pending = -1;  /* second byte of arrow sequence  */
static int            s_peeked  = -1;  /* byte stashed by platform_kbhit */

/* ── Raw mode lifecycle ─────────────────────────────────────────────────── */
static void s_raw_off(void) {
    if (!s_raw_on) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &s_saved);
    s_raw_on = 0;
}

/* Called once on first getch/kbhit. Registered with atexit so the terminal
   is always restored even if the app exits abnormally. */
static void s_raw_enable(void) {
    if (s_raw_on) return;
    tcgetattr(STDIN_FILENO, &s_saved);
    struct termios newt = s_saved;
    newt.c_lflag &= ~(ICANON | ECHO);
    newt.c_cc[VMIN]  = 1;
    newt.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    atexit(s_raw_off);
    s_raw_on = 1;
}

/* ── Byte reader ────────────────────────────────────────────────────────── */
/* timeout_ms < 0 = blocking; >= 0 = wait at most that many ms then return -1 */
static int s_read_byte(int timeout_ms) {
    if (timeout_ms >= 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0)
            return -1;
    }
    unsigned char buf;
    return (read(STDIN_FILENO, &buf, 1) == 1) ? (int)buf : -1;
}

/* ── Public API ─────────────────────────────────────────────────────────── */
void platform_sleep_ms(unsigned ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

unsigned long long platform_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ULL
         + (unsigned long long)(ts.tv_nsec / 1000000);
}

void platform_get_terminal_size(int *cols, int *rows) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        *cols = w.ws_col;
        *rows = w.ws_row;
    } else {
        *cols = 80;
        *rows = 24;
    }
}

/*
 * platform_getch — drop-in for _getch(), two-byte arrow protocol emulated.
 *
 * Linux sends ESC [ A/B/C/D; Windows sends 224 then 72/80/75/77.
 * We return 224 first, stash the arrow code in s_pending for the next call.
 */
int platform_getch(void) {
    s_raw_enable();

    if (s_pending != -1) { int k = s_pending; s_pending = -1; return k; }

    int ch;
    if (s_peeked != -1) { ch = s_peeked; s_peeked = -1; }
    else                 { ch = s_read_byte(-1); }  /* blocking */

    if (ch == 27) {
        int ch2 = s_read_byte(50);   /* 50ms — terminal flushes ESC sequence fast */
        if (ch2 == '[') {
            int ch3 = s_read_byte(50);
            switch (ch3) {
                case 'A': s_pending = 72; return 224;
                case 'B': s_pending = 80; return 224;
                case 'C': s_pending = 77; return 224;
                case 'D': s_pending = 75; return 224;
            }
        }
        return 27;
    }

    if (ch == '\n') return '\r';
    if (ch == 127)  return 8;
    return ch;
}

int platform_kbhit(void) {
    s_raw_enable();
    if (s_pending != -1 || s_peeked != -1) return 1;
    int ch = s_read_byte(0);   /* 0ms = non-blocking */
    if (ch != -1) { s_peeked = ch; return 1; }
    return 0;
}

#endif /* !_WIN32 */
