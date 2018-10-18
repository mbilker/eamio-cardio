#include <stdarg.h>
#include <stdio.h>

#include "bemanitools/glue.h"
#include "bemanitools/eamio.h"

#include "hid.h"
#include "log.h"

#define DLLEXPORT __declspec(dllexport)

log_formatter_t misc_ptr;
log_formatter_t info_ptr;
log_formatter_t warning_ptr;
log_formatter_t fatal_ptr;

uint8_t ID_TIMER[2] = { 0, 0 };
uint8_t LAST_CARD_TYPE[2] = { 0, 0 };

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

typedef void (*super_eam_io_set_loggers_t)(log_formatter_t, log_formatter_t, log_formatter_t, log_formatter_t);
typedef bool (*super_eam_io_init_t)(thread_create_t, thread_join_t, thread_destroy_t);
typedef uint16_t (*super_eam_io_get_keypad_state_t)(uint8_t);
typedef uint8_t (*super_eam_io_get_sensor_state_t)(uint8_t);
typedef uint8_t (*super_eam_io_read_card_t)(uint8_t, uint8_t *, uint8_t);
typedef bool (*super_eam_io_poll_t)(uint8_t);
typedef void (*super_eam_io_fini_t)(void);
typedef struct eam_io_config_api *(*super_eam_io_get_config_api_t)(void);

bool orig_eam_io_load_attempted = false;
bool orig_eam_io_initialized = false;
bool orig_eam_io_handle_card_read = false;

HMODULE orig_eam_io_handle = NULL;
super_eam_io_set_loggers_t super_eam_io_set_loggers = NULL;
super_eam_io_init_t super_eam_io_init = NULL;
super_eam_io_get_keypad_state_t super_eam_io_get_keypad_state = NULL;
super_eam_io_get_sensor_state_t super_eam_io_get_sensor_state = NULL;
super_eam_io_read_card_t super_eam_io_read_card = NULL;
super_eam_io_poll_t super_eam_io_poll = NULL;
super_eam_io_fini_t super_eam_io_fini = NULL;
super_eam_io_get_config_api_t super_eam_io_get_config_api = NULL;

static bool load_orig_eamio() {
  if (orig_eam_io_load_attempted) {
    return orig_eam_io_handle != NULL;
  }

  if (orig_eam_io_handle == NULL) {
    orig_eam_io_load_attempted = true;
    orig_eam_io_handle = LoadLibrary(TEXT("eamio_orig.dll"));

    if (orig_eam_io_handle == NULL) {
      fatal_log_f("Failed to load eamio_orig.dll: 0x%08x", GetLastError());
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
LOAD_ORIG_EAMIO_FUNC(eam_io_get_sensor_state);
LOAD_ORIG_EAMIO_FUNC(eam_io_read_card);
LOAD_ORIG_EAMIO_FUNC(eam_io_poll);
LOAD_ORIG_EAMIO_FUNC(eam_io_fini);
LOAD_ORIG_EAMIO_FUNC(eam_io_get_config_api);

void DLLEXPORT eam_io_set_loggers(log_formatter_t misc, log_formatter_t info, log_formatter_t warning, log_formatter_t fatal) {
  misc_ptr = misc;
  info_ptr = info;
  warning_ptr = warning;
  fatal_ptr = fatal;

  if (load_orig_eamio() && load_eam_io_set_loggers()) {
    super_eam_io_set_loggers(misc, info, warning, fatal);
  }
}

bool DLLEXPORT eam_io_init(thread_create_t thread_create, thread_join_t thread_join, thread_destroy_t thread_destroy) {
  info_ptr("cardio", "HID Card Reader v1.3 (r" GIT_REVISION " " GIT_COMMIT ") by Felix");

  if (load_orig_eamio()) {
    if (!load_eam_io_init()) { return false; }
    if (!load_eam_io_get_keypad_state()) { return false; }
    if (!load_eam_io_get_sensor_state()) { return false; }
    if (!load_eam_io_read_card()) { return false; }
    if (!load_eam_io_poll()) { return false; }
    if (!load_eam_io_fini()) { return false; }

    if (super_eam_io_init(thread_create, thread_join, thread_destroy)) {
      orig_eam_io_initialized = true;
    } else {
      warning_ptr("cardio", "Failed to initialize eamio_orig.dll");
      return false;
    }

    orig_eam_io_initialized = true;
  }

  info_ptr("cardio", "Initializing HID card reader");

  set_log_func(info_log_f);

  hid_init();
  if (!hid_scan()) {
    warning_ptr("cardio", "Failed to initialize HID card reader");
    return false;
  }
}

void DLLEXPORT eam_io_fini(void) {
  if (orig_eam_io_initialized && super_eam_io_fini) {
    super_eam_io_fini();
  }

  hid_close();
}

uint16_t DLLEXPORT eam_io_get_keypad_state(uint8_t unit_no) {
  if (orig_eam_io_initialized && super_eam_io_get_keypad_state) {
    return super_eam_io_get_keypad_state(unit_no);
  }

  return 0;
}

uint8_t DLLEXPORT eam_io_get_sensor_state(uint8_t unit_no) {
  bool checked_orig_eam_io = false;
  uint8_t result = 0;
  size_t i, j;

  // Disable card reading from the original eamio.dll when it returns zero
  // from `eam_io_get_sensor_state`
  if (orig_eam_io_handle_card_read &&
      orig_eam_io_initialized &&
      super_eam_io_get_sensor_state)
  {
    result = super_eam_io_get_sensor_state(unit_no);

    if (result == 0) {
      orig_eam_io_handle_card_read = false;
      checked_orig_eam_io = true;
    } else {
      return result;
    }
  }

  EnterCriticalSection(&crit_section);

  if (unit_no < contexts_length) {
    if (ID_TIMER[unit_no]) {
      DEBUG_LOG("ID_TIMER[%u]: %u", unit_no, ID_TIMER[unit_no]);

      ID_TIMER[unit_no]--;
      result = 3;
    } else if (contexts[unit_no].initialized) {
      switch (hid_device_poll(&contexts[unit_no])) {
        case HID_POLL_ERROR:
          fatal_ptr("cardio", "Error polling device");
          result = 0;
          break;

        case HID_POLL_CARD_NOT_READY:
          result = 0;
          break;

        case HID_POLL_CARD_READY:
          result = 3;
          break;
      }
    }
  }

  LeaveCriticalSection(&crit_section);

  if (result == 0 &&
      !checked_orig_eam_io &&
      orig_eam_io_initialized &&
      super_eam_io_get_sensor_state)
  {
    orig_eam_io_handle_card_read = true;

    return super_eam_io_get_sensor_state(unit_no);
  }

  return result;
}

uint8_t DLLEXPORT eam_io_read_card(uint8_t unit_no, uint8_t *card_id, uint8_t nbytes) {
  uint8_t result = EAM_IO_CARD_NONE;

  if (orig_eam_io_handle_card_read && orig_eam_io_initialized && super_eam_io_read_card) {
    info_ptr("cardio", "Reading card with eamio_orig.dll");
    return super_eam_io_read_card(unit_no, card_id, nbytes);
  }

  EnterCriticalSection(&crit_section);

  if (unit_no < contexts_length && ID_TIMER[unit_no]) {
    memcpy(card_id, contexts[unit_no].usage_value, nbytes);
    result = LAST_CARD_TYPE[unit_no];
  }

  if (unit_no < contexts_length && result == EAM_IO_CARD_NONE) {
    uint8_t card_type = hid_device_read(&contexts[unit_no]);

    if (nbytes > sizeof(contexts[unit_no].usage_value)) {
      fatal_ptr("cardio", "nbytes > buffer_size");
      card_type = HID_CARD_NONE;
    }

    switch (card_type) {
      case HID_CARD_NONE:
        result = EAM_IO_CARD_NONE;
        break;

      case HID_CARD_ISO_15693:
        info_ptr("cardio", "Found: EAM_IO_CARD_ISO15696");
        result = EAM_IO_CARD_ISO15696;
        break;

      case HID_CARD_ISO_18092:
        info_ptr("cardio", "Found: EAM_IO_CARD_FELICA");
        result = EAM_IO_CARD_FELICA;
        break;

      default:
        warning_ptr("cardio", "Unknown card type found");
        result = EAM_IO_CARD_NONE;
    }

    if (result != EAM_IO_CARD_NONE) {
      memcpy(card_id, contexts[unit_no].usage_value, nbytes);
      ID_TIMER[unit_no] = 32;
      LAST_CARD_TYPE[unit_no] = result;
    }
  }

  LeaveCriticalSection(&crit_section);

  return result;
}

bool DLLEXPORT eam_io_card_slot_cmd(uint8_t unit_no, uint8_t cmd) {
  return false;
}

bool DLLEXPORT eam_io_poll(uint8_t unit_no) {
  if (orig_eam_io_initialized && super_eam_io_poll) {
    super_eam_io_poll(unit_no);
  }

  return true;
}

const struct eam_io_config_api * DLLEXPORT eam_io_get_config_api(void) {
  if (load_orig_eamio() && load_eam_io_get_config_api()) {
    return super_eam_io_get_config_api();
  }

  return NULL;
}
