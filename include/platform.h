#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
  #include <windows.h>
  #include <conio.h>
  #include <io.h>
  #include <direct.h>
  static inline void               platform_sleep_ms(unsigned ms)    { Sleep(ms); }
  static inline int                platform_getch(void)               { return _getch(); }
  static inline int                platform_kbhit(void)               { return _kbhit(); }
  static inline unsigned long long platform_now_ms(void)              { return (unsigned long long)GetTickCount64(); }
#else
  #include <termios.h>
  #include <unistd.h>
  #include <time.h>
  #include <sys/ioctl.h>
  #include <strings.h>      /* strcasecmp, strncasecmp */
  #include <ctype.h>
  #include <sys/stat.h>
  #include <dirent.h>

  void               platform_sleep_ms(unsigned ms);
  int                platform_getch(void);
  int                platform_kbhit(void);
  unsigned long long platform_now_ms(void);
  void               platform_get_terminal_size(int *cols, int *rows);

  /* Drop-in replacements — existing call sites need no changes */
  #define _getch()           platform_getch()
  #define getch()            platform_getch()
  #define _kbhit()           platform_kbhit()
  #define Sleep(ms)          platform_sleep_ms((unsigned)(ms))

  /* Windows string/memory functions → POSIX equivalents */
  #define _strdup(s)         strdup(s)
  #define _stricmp(a,b)      strcasecmp((a),(b))
  #define _strnicmp(a,b,n)   strncasecmp((a),(b),(n))

  /* Windows safe-string functions → simplified POSIX versions */
  #define strcpy_s(d,n,s)    (strncpy((d),(s),(n)-1), (d)[(n)-1]='\0')
  #define strncpy_s(d,n,s,c) (strncpy((d),(s),((c)==(size_t)-1?(n)-1:(c))), (d)[(n)-1]='\0')
  #define scanf_s            scanf
  #define sscanf_s           sscanf
  #define fscanf_s           fscanf
  #define sprintf_s(d,n,...) snprintf((d),(n),__VA_ARGS__)
  #define _TRUNCATE          ((size_t)-1)
  #define _countof(a)        (sizeof(a)/sizeof((a)[0]))

  /* Windows directory/file functions → POSIX */
  #define _mkdir(path)       mkdir((path), 0755)

  /* Windows console startup — no-ops on Linux */
  static inline void win_enable_utf8_console(void) {}

#endif /* _WIN32 */

#endif /* PLATFORM_H */
