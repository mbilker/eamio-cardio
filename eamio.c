#include <stdarg.h>
#include <stdio.h>

#include "bemanitools/glue.h"
#include "bemanitools/eamio.h"

#include "hid.h"

log_formatter_t misc_ptr;
log_formatter_t info_ptr;
log_formatter_t warning_ptr;
log_formatter_t fatal_ptr;

struct eamio_hid_device hid_ctx;

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
typedef void (*super_eam_io_init_t)(thread_create_t, thread_join_t, thread_destroy_t);
typedef uint16_t (*super_eam_io_get_keypad_state_t)(uint8_t);
typedef bool (*super_eam_io_poll_t)(uint8_t);
typedef void (*super_eam_io_fini_t)(void);
typedef struct eam_io_config_api *(*super_eam_io_get_config_api_t)(void);

bool orig_eam_io_load_attempted = false;

HMODULE orig_eam_io_handle = NULL;
super_eam_io_set_loggers_t super_eam_io_set_loggers = NULL;
super_eam_io_init_t super_eam_io_init = NULL;
super_eam_io_get_keypad_state_t super_eam_io_get_keypad_state = NULL;
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
LOAD_ORIG_EAMIO_FUNC(eam_io_poll);
LOAD_ORIG_EAMIO_FUNC(eam_io_fini);
LOAD_ORIG_EAMIO_FUNC(eam_io_get_config_api);

void eam_io_set_loggers(log_formatter_t misc, log_formatter_t info, log_formatter_t warning, log_formatter_t fatal) {
  misc_ptr = misc;
  info_ptr = info;
  warning_ptr = warning;
  fatal_ptr = fatal;

  if (load_orig_eamio() && load_eam_io_set_loggers()) {
    super_eam_io_set_loggers(misc, info, warning, fatal);
  }
}

bool eam_io_init(thread_create_t thread_create, thread_join_t thread_join, thread_destroy_t thread_destroy) {
  if (load_orig_eamio()) {
    if (!load_eam_io_init()) { return 0; }
    if (!load_eam_io_get_keypad_state()) { return 0; }
    if (!load_eam_io_poll()) { return 0; }
    if (!load_eam_io_fini()) { return 0; }

    super_eam_io_init(thread_create, thread_join, thread_destroy);
  }

  info_ptr("cardio", "initializing HID card reader");

  set_log_func(info_log_f);

  hid_ctx_init(&hid_ctx);
  if (hid_scan(&hid_ctx)) {
    info_ptr("cardio", "HID card reader initialized");
    return true;
  } else {
    fatal_ptr("cardio", "Failed to initialize HID card reader");
    return false;
  }
}

void eam_io_fini(void) {
  if (super_eam_io_fini) {
    super_eam_io_fini();
  }

  hid_free(&hid_ctx);
}

uint16_t eam_io_get_keypad_state(uint8_t unit_no) {
  if (super_eam_io_get_keypad_state) {
    return super_eam_io_get_keypad_state(unit_no);
  }

  return 0;
}

uint8_t eam_io_get_sensor_state(uint8_t unit_no) {
  if (unit_no >= 2) {
    return 0;
  }

  switch (hid_device_poll(&hid_ctx)) {
    case HID_POLL_ERROR:
      fatal_ptr("cardio", "Error polling device");
      return 0;

    case HID_POLL_CARD_NOT_READY:
      return 0;

    case HID_POLL_CARD_READY:
      return 3;
  }

  return 0;
}

uint8_t eam_io_read_card(uint8_t unit_no, uint8_t *card_id, uint8_t nbytes) {
  info_ptr("cardio", "eam_io_read_card");

  if (unit_no >= 2) {
    return EAM_IO_CARD_NONE;
  }

  uint8_t card_type = hid_device_read(&hid_ctx);

  if (nbytes > sizeof(hid_ctx.usage_value)) {
    fatal_ptr("cardio", "nbytes > buffer_size");
    return EAM_IO_CARD_NONE;
  }
  memcpy(card_id, hid_ctx.usage_value, nbytes);

  switch (card_type) {
    case HID_CARD_NONE:
      return EAM_IO_CARD_NONE;
    case HID_CARD_ISO_15693:
      info_ptr("cardio", "Found: EAM_IO_CARD_ISO15696");
      return EAM_IO_CARD_ISO15696;
    case HID_CARD_ISO_18092:
      info_ptr("cardio", "Found: EAM_IO_CARD_FELICA");
      return EAM_IO_CARD_FELICA;
    default:
      warning_ptr("cardio", "Error retrieving card id");
      return EAM_IO_CARD_NONE;
  }
}

bool eam_io_card_slot_cmd(uint8_t unit_no, uint8_t cmd) {
  return false;
}

bool eam_io_poll(uint8_t unit_no) {
  if (super_eam_io_poll) {
    super_eam_io_poll(unit_no);
  }

  return true;
}

const struct eam_io_config_api *eam_io_get_config_api(void) {
  if (load_orig_eamio() && load_eam_io_get_config_api()) {
    return super_eam_io_get_config_api();
  }

  return NULL;
}
