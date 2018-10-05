typedef void (*log_func_t)(const char *fmt, ...)
  __attribute__(( format(printf, 1, 2) ));

extern log_func_t log_f;

void set_log_func(log_func_t log_func);
