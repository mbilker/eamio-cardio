#include <stdarg.h>
#include <stdio.h>

#include "bemanitools/glue.h"
#include "bemanitools/eamio.h"

#include "drive_check.h"
#include "window.h"

log_formatter_t misc_ptr;
log_formatter_t info_ptr;
log_formatter_t warning_ptr;
log_formatter_t fatal_ptr;

thread_create_t thread_create_ptr;
thread_join_t thread_join_ptr;
thread_destroy_t thread_destroy_ptr;

int message_pump_thread;
HWND hWnd;

void info_log_f(const char *fmt, ...) {
  char msg[512];
  va_list args;
  va_start(args, fmt);
  vsprintf(msg, fmt, args);
  info_ptr("cardio", msg);
  va_end(args);
}

void fatal_log_f(const char *fmt, ...) {
  char msg[512];
  va_list args;
  va_start(args, fmt);
  vsprintf(msg, fmt, args);
  fatal_ptr("cardio", msg);
  va_end(args);
}

#ifdef EAMIO_DEBUG
#define DEBUG_LOG info_log_f
#else
#define DEBUG_LOG
#endif

#ifdef WITH_ORIG_EAMIO
typedef void (*super_eam_io_set_loggers_t)(log_formatter_t, log_formatter_t, log_formatter_t, log_formatter_t);
typedef void (*super_eam_io_init_t)(thread_create_t, thread_join_t, thread_destroy_t);
typedef uint16_t (*super_eam_io_get_keypad_state_t)(uint8_t);
typedef bool (*super_eam_io_poll_t)(uint8_t);
typedef void (*super_eam_io_fini_t)(void);
typedef struct eam_io_config_api *(*super_eam_io_get_config_api_t)(void);

HMODULE orig_eam_io_handle = NULL;
super_eam_io_set_loggers_t super_eam_io_set_loggers = NULL;
super_eam_io_init_t super_eam_io_init = NULL;
super_eam_io_get_keypad_state_t super_eam_io_get_keypad_state = NULL;
super_eam_io_poll_t super_eam_io_poll = NULL;
super_eam_io_fini_t super_eam_io_fini = NULL;
super_eam_io_get_config_api_t super_eam_io_get_config_api = NULL;

static bool load_orig_eamio() {
  if (orig_eam_io_handle == NULL) {
    orig_eam_io_handle = LoadLibrary(TEXT("eamio_orig.dll"));

    if (orig_eam_io_handle == NULL) {
      fatal_log_f("Failed to load eamio_orig.dll: %lu", GetLastError());
      return 0;
    }

    DEBUG_LOG("Loaded eamio_orig.dll");
  }

  return 1;
}

#define LOAD_ORIG_EAMIO_FUNC(NAME) \
static bool load_ ## NAME() { \
  if ((super_ ## NAME) == NULL) { \
    super_ ## NAME = (super_ ## NAME ## _t) GetProcAddress(orig_eam_io_handle, #NAME); \
    if ((super_ ## NAME) == NULL) { \
      fatal_log_f("Failed to load " #NAME " from eamio_orig.dll: %lu", GetLastError()); \
      return 0; \
    } \
    DEBUG_LOG("Loaded " #NAME " from eamio_orig.dll: %p", super_ ## NAME); \
  } \
\
  return 1; \
}

LOAD_ORIG_EAMIO_FUNC(eam_io_set_loggers);
LOAD_ORIG_EAMIO_FUNC(eam_io_init);
LOAD_ORIG_EAMIO_FUNC(eam_io_get_keypad_state);
LOAD_ORIG_EAMIO_FUNC(eam_io_poll);
LOAD_ORIG_EAMIO_FUNC(eam_io_fini);
LOAD_ORIG_EAMIO_FUNC(eam_io_get_config_api);
#endif

void eam_io_set_loggers(log_formatter_t misc, log_formatter_t info, log_formatter_t warning, log_formatter_t fatal) {
  misc_ptr = misc;
  info_ptr = info;
  warning_ptr = warning;
  fatal_ptr = fatal;

#ifdef WITH_ORIG_EAMIO
  if (!load_orig_eamio() || !load_eam_io_set_loggers()) {
    return;
  }
  super_eam_io_set_loggers(misc, info, warning, fatal);
#endif
}

int thread_message_pump(void *ctx) {
  if (!InitWindowClass()) {
    fatal_ptr("cardio", "Failed to initialize window class");
    return -1;
  }

  HINSTANCE hInstance = GetModuleHandle(NULL);
  if ((hWnd = CreateTheWindow(hInstance)) == NULL) {
    fatal_ptr("cardio", "Failed to initialize background window");
    return -1;
  }

  info_log_f("Drive insertion listener ready, thread id = %lu", GetCurrentThreadId());

  if (!MessagePump(hWnd)) {
    fatal_ptr("cardio", "Message pump error");
    return -1;
  }

  return 0;
}

bool eam_io_init(thread_create_t thread_create, thread_join_t thread_join, thread_destroy_t thread_destroy) {
#ifdef WITH_ORIG_EAMIO
  if (!load_orig_eamio()) { return 0; }
  if (!load_eam_io_init()) { return 0; }
  if (!load_eam_io_get_keypad_state()) { return 0; }
  if (!load_eam_io_poll()) { return 0; }
  if (!load_eam_io_fini()) { return 0; }

  super_eam_io_init(thread_create, thread_join, thread_destroy);
#endif

  set_log_func(info_log_f);

  message_pump_thread = thread_create(thread_message_pump, NULL, 0x4000, 0);

  thread_create_ptr = thread_create;
  thread_join_ptr = thread_join;
  thread_destroy_ptr = thread_destroy;

  return 1;
}

void eam_io_fini(void) {
  int result;

#ifdef WITH_ORIG_EAMIO
  if (super_eam_io_fini != NULL) {
    super_eam_io_fini();
  }
#endif

  EndTheWindow(hWnd);

  info_ptr("cardio", "Message pump thread shutting down");
  thread_join_ptr(message_pump_thread, &result);
}

uint16_t eam_io_get_keypad_state(uint8_t unit_no) {
#ifdef WITH_ORIG_EAMIO
  return super_eam_io_get_keypad_state(unit_no);
#else
  return 0;
#endif
}

uint8_t eam_io_get_sensor_state(uint8_t unit_no) {
  if (unit_no >= 2) {
    return 0;
  }
  if (ID_TIMER[unit_no]) {
    ID_TIMER[unit_no]--;
    return 3;
  }
  return 0;
}

uint8_t eam_io_read_card(uint8_t unit_no, uint8_t *card_id, uint8_t nbytes) {
  size_t i;

  if (unit_no >= 2) {
    return EAM_IO_CARD_NONE;
  }
  memcpy(card_id, ID[unit_no], nbytes);

  info_log_f("lo: %08x", *(uint32_t *) card_id);
  info_log_f("hi: %08x", *(uint32_t *)(card_id + 4));

  if (card_id[0] == 0xE0 && card_id[1] == 0x04) {
    info_ptr("cardio", "Found: EAM_IO_CARD_ISO15696");
    return EAM_IO_CARD_ISO15696;
  } else {
    info_ptr("cardio", "Found: EAM_IO_CARD_FELICA");
    return EAM_IO_CARD_FELICA;
  }
}

bool eam_io_card_slot_cmd(uint8_t unit_no, uint8_t cmd) {
  return false;
}

bool eam_io_poll(uint8_t unit_no) {
#ifdef WITH_ORIG_EAMIO
  return super_eam_io_poll(unit_no);
#else
  return true;
#endif
}

const struct eam_io_config_api *eam_io_get_config_api(void) {
#ifdef WITH_ORIG_EAMIO
  if (!load_orig_eamio() || !load_eam_io_get_config_api()) {
    return NULL;
  }

  return super_eam_io_get_config_api();
#else
  return NULL;
#endif
}
