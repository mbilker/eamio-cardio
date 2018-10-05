#include <windows.h>
#include <stdio.h>
#include <strsafe.h>

#include "log.h"

static void log_default(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

log_func_t log_f = log_default;

void set_log_func(log_func_t log_func) {
  log_f = log_func;
}
