#ifndef _HID_H
#define _HID_H

#include <windows.h>
#include <stdint.h>
#include <hidsdi.h>

extern CRITICAL_SECTION crit_section;
extern struct eamio_hid_device *contexts;
extern size_t contexts_length;

struct eamio_hid_device {
  LPWSTR dev_path;
  HANDLE dev_handle;
  OVERLAPPED read_state;
  BOOL initialized;
  BOOL io_pending;

  BYTE report_buffer[128];
  unsigned char usage_value[128];
  DWORD read_size;

  PHIDP_PREPARSED_DATA pp_data;
  HIDP_CAPS caps;
  PHIDP_VALUE_CAPS collection;
  USHORT collection_length;
};

typedef enum hid_poll_value {
  HID_POLL_ERROR = 0,
  HID_POLL_CARD_NOT_READY = 1,
  HID_POLL_CARD_READY = 2,
} hid_poll_value_t;

typedef enum hid_card_type {
  HID_CARD_NONE = 0,
  HID_CARD_ISO_15693 = 0x41,
  HID_CARD_ISO_18092 = 0x42,
} hid_card_type_t;

BOOL hid_init();
void hid_close();
BOOL hid_add_device(LPCWSTR device_path);
BOOL hid_remove_device(LPCWSTR device_path);
BOOL hid_scan_device(struct eamio_hid_device *ctx, LPCWSTR device_path);
BOOL hid_scan();
hid_poll_value_t hid_device_poll(struct eamio_hid_device *ctx);
uint8_t hid_device_read(struct eamio_hid_device *ctx);

#ifdef HID_DEBUG
void hid_print_contexts();
#endif

#endif
