#include <stdarg.h>
#include <stdio.h>

#include "bemanitools/glue.h"
#include "bemanitools/eamio.h"

#include "hid.h"
#include "log.h"
#include "window.h"

#define DLLEXPORT __declspec(dllexport)

log_formatter_t misc_ptr;
log_formatter_t info_ptr;
log_formatter_t warning_ptr;
log_formatter_t fatal_ptr;

thread_create_t thread_create_ptr;
thread_join_t thread_join_ptr;
thread_destroy_t thread_destroy_ptr;

int message_pump_thread;
BOOL message_pump_ready = FALSE;
HWND hWnd;

struct card_timer_holder {
  size_t index;
  uint8_t ticks;
  uint8_t last_card_type;
};

uint8_t MAX_NUM_OF_READERS = 1;
struct card_timer_holder *ID_TIMER = NULL;

void info_log_f(const char *fmt, ...) {
  char msg[512];
  va_list args;
  va_start(args, fmt);
  vsprintf(msg, fmt, args);
  info_ptr("cardio", msg);
  va_end(args);
}

void warning_log_f(const char *fmt, ...) {
  char msg[512];
  va_list args;
  va_start(args, fmt);
  vsprintf(msg, fmt, args);
  warning_ptr("cardio", msg);
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
#define DEBUG_LOG(MSG, ...) info_log_f("[DEBUG] " MSG, ##__VA_ARGS__)
#else
#define DEBUG_LOG(MSG, ...)
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
      warning_log_f("Failed to load eamio_orig.dll: %08lx", GetLastError());
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
      warning_log_f("Failed to load " #NAME " from eamio_orig.dll: %08lx", GetLastError()); \
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

int thread_message_pump(void *ctx) {
  HINSTANCE hInstance;

  if (!InitWindowClass()) {
    warning_ptr("cardio", "Failed to initialize window class");
    return -1;
  }

  hInstance = GetModuleHandle(NULL);
  if ((hWnd = CreateTheWindow(hInstance)) == NULL) {
    warning_ptr("cardio", "Failed to initialize the background window");
    return -1;
  }

  info_log_f("Device notification listener ready, thread id = %lu", GetCurrentThreadId());
  message_pump_ready = TRUE;

  if (!MessagePump(hWnd)) {
    warning_ptr("cardio", "Message pump error");
    return -1;
  }

  return 0;
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

  thread_create_ptr = thread_create;
  thread_join_ptr = thread_join;
  thread_destroy_ptr = thread_destroy;

  ID_TIMER = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_NUM_OF_READERS * sizeof(struct card_timer_holder));
  if (ID_TIMER == NULL) {
    warning_ptr("cardio", "Failed to allocate memory for card id staging");
    return false;
  }

  hid_init();

  if (!hid_scan()) {
    warning_ptr("cardio", "Failed to initialize HID card reader");
    return false;
  }

  message_pump_thread = thread_create(thread_message_pump, NULL, 0x4000, 0);

  while (message_pump_ready == FALSE) {
    Sleep(25);
  }

  return true;
}

void DLLEXPORT eam_io_fini(void) {
  int result;

  if (orig_eam_io_initialized && super_eam_io_fini) {
    super_eam_io_fini();
  }

  HeapFree(GetProcessHeap(), 0, ID_TIMER);

  EndTheWindow(hWnd);

  info_ptr("cardio", "Device notification thread shutting down");
  thread_join_ptr(message_pump_thread, &result);

  hid_close();
}

uint16_t DLLEXPORT eam_io_get_keypad_state(uint8_t unit_no) {
  if (orig_eam_io_initialized && super_eam_io_get_keypad_state) {
    return super_eam_io_get_keypad_state(unit_no);
  }

  return 0;
}

uint8_t DLLEXPORT eam_io_get_sensor_state(uint8_t unit_no) {
  uint8_t result = 0;
  size_t i;
  struct eamio_hid_device *ctx;
  struct card_timer_holder *id_timer;

  // `MAX_NUM_OF_READERS` starts at 1, but `unit_no` is zero indexed, add 1 to `unit_no` so it
  // starts at 1.
  if (unit_no + 1 > MAX_NUM_OF_READERS) {
    MAX_NUM_OF_READERS = unit_no + 1;

    info_log_f("Max number of game readers found is now %u", MAX_NUM_OF_READERS);

    ID_TIMER = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ID_TIMER, MAX_NUM_OF_READERS * sizeof(struct card_timer_holder));
    if (ID_TIMER == NULL) {
      fatal_ptr("cardio", "Failed to reallocate memory for card id staging");
      return 0;
    }
  }
  id_timer = &ID_TIMER[unit_no];

  if (id_timer->ticks > 0) {
    DEBUG_LOG("ID_TIMER[%u]: %u", unit_no, id_timer->ticks);

    id_timer->ticks--;
    return 3;
  }

  EnterCriticalSection(&HID_LOCK);

  for (i = unit_no; i < CONTEXTS_LENGTH; i += MAX_NUM_OF_READERS) {
    ctx = &CONTEXTS[i];

    if (ctx->initialized) {
      switch (hid_device_poll(ctx)) {
        case HID_POLL_ERROR:
          warning_ptr("cardio", "Error polling device");
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

    if (result != 0) {
      break;
    }
  }

  LeaveCriticalSection(&HID_LOCK);

  // Disable card reading from the original eamio.dll when it returns zero
  // from `eam_io_get_sensor_state`
  if (result == 0 &&
      orig_eam_io_handle_card_read &&
      orig_eam_io_initialized &&
      super_eam_io_get_sensor_state)
  {
    result = super_eam_io_get_sensor_state(unit_no);
    orig_eam_io_handle_card_read = result != 0;
  }

  return result;
}

uint8_t DLLEXPORT eam_io_read_card(uint8_t unit_no, uint8_t *card_id, uint8_t nbytes) {
  uint8_t result = EAM_IO_CARD_NONE;
  struct card_timer_holder *id_timer = &ID_TIMER[unit_no];
  struct eamio_hid_device *ctx;
  size_t i;

  DEBUG_LOG("eam_io_read_card(unit_no: %u)", unit_no);

  if (orig_eam_io_handle_card_read && orig_eam_io_initialized && super_eam_io_read_card) {
    info_ptr("cardio", "Reading card with eamio_orig.dll");
    return super_eam_io_read_card(unit_no, card_id, nbytes);
  }

  EnterCriticalSection(&HID_LOCK);

  if (id_timer->ticks > 0) {
    ctx = &CONTEXTS[id_timer->index];

    memcpy(card_id, ctx->usage_value, nbytes);
    result = id_timer->last_card_type;
  } else {
    // The idea here is to map excess HID readers to the modulo of the maximum number of readers
    // the game uses, which in most cases is two.
    for (i = unit_no; i < CONTEXTS_LENGTH; i += MAX_NUM_OF_READERS) {
      ctx = &CONTEXTS[i];

      // Skip uninitialized contexts
      if (!ctx->initialized) {
        continue;
      }

      uint8_t card_type = hid_device_read(ctx);

      if (nbytes > sizeof(ctx->usage_value)) {
        warning_ptr("cardio", "nbytes > buffer_size, not inserting card");
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
          warning_log_f("cardio", "Unknown card type found: %u", card_type);
          result = EAM_IO_CARD_NONE;
      }

      // If a card was grabbed, store the reader index in the timer structure for 32
      // reading ticks along with the card type. The index is needed as multiple HID
      // readers may be mapped to a single game reader unit.
      if (result != EAM_IO_CARD_NONE) {
        memcpy(card_id, ctx->usage_value, nbytes);
        id_timer->index = i;
        id_timer->ticks = 32;
        id_timer->last_card_type = result;

        break;
      }
    }
  }

  LeaveCriticalSection(&HID_LOCK);

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
