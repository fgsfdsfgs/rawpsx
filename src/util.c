#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

extern void abort(void) __attribute__((noreturn));

// while our libc has a declaration for assert(), it does not actually provide it

void do_assert(const int expr, const char *strexpr, const char *file, const int line) {
  if (!expr) {
    printf("ASSERTION FAILED:\n`%s` at %s:%d\n", strexpr, file, line);
    abort();
  }
}

void panic(const char *fmt, ...) {
  char msg[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);
  printf("FATAL ERROR: %s\n", msg);
  abort();
}
