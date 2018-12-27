#include <windows.h>
#include <devguid.h>
#include <hidclass.h>
#include <hidusage.h>
#include <hidpi.h>
#include <hidsdi.h>
#include <setupapi.h>

#include "hid.h"
#include "log.h"

#ifdef HID_DEBUG
#define DEBUG_LOG(MSG, ...) log_f("[DEBUG] " MSG, ##__VA_ARGS__)
#else
#define DEBUG_LOG
#endif

#define DEFAULT_ALLOCATED_CONTEXTS 2
#define CARD_READER_USAGE_PAGE 0xffca

// GUID_DEVINTERFACE_HID
//GUID class_interface_guid = { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

// GUID_DEVCLASS_HIDCLASS
GUID hidclass_guid = { 0x745a17a0, 0x74d3, 0x11d0, { 0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda } };

CRITICAL_SECTION crit_section;

struct eamio_hid_device *contexts = NULL;
size_t contexts_length = 0;

void hid_ctx_init(struct eamio_hid_device *ctx) {
  ctx->dev_path = NULL;
  ctx->dev_handle = INVALID_HANDLE_VALUE;
  ctx->initialized = FALSE;
  ctx->io_pending = FALSE;
  ctx->read_size = 0;
  ctx->pp_data = NULL;
  ctx->collection = NULL;
  ctx->collection_length = 0;

  memset(&ctx->read_state, 0, sizeof(OVERLAPPED));
  memset(&ctx->report_buffer, 0, sizeof(ctx->report_buffer));
  memset(&ctx->usage_value, 0, sizeof(ctx->usage_value));
  memset(&ctx->caps, 0, sizeof(HIDP_CAPS));
}

void hid_ctx_free(struct eamio_hid_device *ctx) {
  if (ctx->dev_path != NULL) {
    HeapFree(GetProcessHeap(), 0, ctx->dev_path);
    ctx->dev_path = NULL;
  }

  if (ctx->dev_handle != INVALID_HANDLE_VALUE) {
    CancelIo(ctx->dev_handle);
    CloseHandle(ctx->dev_handle);
    ctx->dev_handle = INVALID_HANDLE_VALUE;
  }

  if (ctx->pp_data != NULL) {
    HidD_FreePreparsedData(ctx->pp_data);
    ctx->pp_data = NULL;
  }

  if (ctx->collection != NULL) {
    HeapFree(GetProcessHeap(), 0, ctx->collection);
    ctx->collection = NULL;
  }
}

void hid_ctx_reset(struct eamio_hid_device *ctx) {
  ctx->initialized = FALSE;
  ctx->io_pending = FALSE;
  ctx->read_size = 0;
  ctx->collection_length = 0;

  hid_ctx_free(ctx);

  memset(&ctx->read_state, 0, sizeof(OVERLAPPED));
  memset(&ctx->report_buffer, 0, sizeof(ctx->report_buffer));
  memset(&ctx->usage_value, 0, sizeof(ctx->usage_value));
  memset(&ctx->caps, 0, sizeof(HIDP_CAPS));
}

BOOL hid_init() {
  size_t i, contexts_size;

  InitializeCriticalSectionAndSpinCount(&crit_section, 0x00000400);

  contexts_size = DEFAULT_ALLOCATED_CONTEXTS * sizeof(struct eamio_hid_device);
  contexts = (struct eamio_hid_device *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, contexts_size);
  if (contexts == NULL) {
    log_f("failed to allocate memory to hold HID device information: %lu", GetLastError());
    return FALSE;
  }

  DEBUG_LOG("contexts[0] = 0x%p", &contexts[0]);
  DEBUG_LOG("contexts[2] = 0x%p", &contexts[2]);
  DEBUG_LOG("contexts + sizeof = 0x%p", ((void *) contexts) + contexts_size);

  contexts_length = DEFAULT_ALLOCATED_CONTEXTS;

  for (i = 0; i < contexts_length; i++) {
    hid_ctx_init(&contexts[i]);
  }
}

void hid_close() {
  size_t i;

  if (contexts_length > 0) {
    for (i = 0; i < contexts_length; i++) {
      hid_ctx_free(&contexts[i]);
    }

    HeapFree(GetProcessHeap(), 0, contexts);
    contexts = NULL;
    contexts_length = 0;

    DeleteCriticalSection(&crit_section);
  }
}

void hid_print_caps(struct eamio_hid_device *hid_ctx) {
#define VPRINT(KEY, VALUE) log_f("... " #KEY ": %u", VALUE)
  VPRINT(InputReportByteLength,     hid_ctx->caps.InputReportByteLength);
  VPRINT(OutputReportByteLength,    hid_ctx->caps.OutputReportByteLength);
  VPRINT(FeatureReportByteLength,   hid_ctx->caps.FeatureReportByteLength);
  VPRINT(NumberLinkCollectionNodes, hid_ctx->caps.NumberLinkCollectionNodes);
  VPRINT(NumberInputValueCaps,      hid_ctx->caps.NumberInputValueCaps);
  VPRINT(NumberInputDataIndices,    hid_ctx->caps.NumberInputDataIndices);
  VPRINT(NumberOutputValueCaps,     hid_ctx->caps.NumberOutputValueCaps);
  VPRINT(NumberOutputDataIndices,   hid_ctx->caps.NumberOutputDataIndices);
  VPRINT(NumberFeatureValueCaps,    hid_ctx->caps.NumberFeatureValueCaps);
  VPRINT(NumberFeatureDataIndices,  hid_ctx->caps.NumberFeatureDataIndices);
#undef VPRINT
}

#ifdef HID_DEBUG
void hid_print_contexts() {
  size_t i;

  EnterCriticalSection(&crit_section);

  for (i = 0; i < contexts_length; i++) {
    struct eamio_hid_device *ctx = &contexts[i];

    DEBUG_LOG("contexts[%Iu] = 0x%p", i, &contexts[i]);
    DEBUG_LOG("... initialized = %d", ctx->initialized);

    if (ctx->initialized) {
      DEBUG_LOG("... dev_path = %ls", ctx->dev_path);
      DEBUG_LOG("... dev_handle = 0x%p", ctx->dev_handle);
    }
  }

  LeaveCriticalSection(&crit_section);
}
#endif

BOOL hid_add_device(LPCWSTR device_path) {
  BOOL res = FALSE;
  size_t i;

  EnterCriticalSection(&crit_section);

  for (i = 0; i < contexts_length; i++) {
    DEBUG_LOG("hid_add_device(\"%ls\") => i: %Iu", device_path, i);

    if (!contexts[i].initialized) {
      res = hid_scan_device(&contexts[i], device_path);
      break;
    }
  }

  LeaveCriticalSection(&crit_section);

  return res;
}

BOOL hid_remove_device(LPCWSTR device_path) {
  BOOL res = FALSE;
  size_t i;

  EnterCriticalSection(&crit_section);

  for (i = 0; i < contexts_length; i++) {
    // The device paths in `hid_scan` are partially lower-case, so perform a
    // case-insensitive comparison here
    if (contexts[i].initialized && (wcsicmp(device_path, contexts[i].dev_path) == 0)) {
      DEBUG_LOG("hid_remove_device(\"%ls\") => i: %Iu", device_path, i);

      hid_ctx_reset(&contexts[i]);

      res = TRUE;
      break;
    }
  }

  LeaveCriticalSection(&crit_section);

  return res;
}

/*
 * Scan HID device to see if it is a HID reader
 */
BOOL hid_scan_device(struct eamio_hid_device *ctx, LPCWSTR device_path) {
  size_t i, dev_path_size;
  NTSTATUS res;
  DWORD error = 0;
  DWORD length = 0;
  LPWSTR pBuffer = NULL;

  dev_path_size = (wcslen(device_path) + 1) * sizeof(WCHAR);
  DEBUG_LOG("hid_scan_device(\"%ls\") => dev_path_size: %Iu", device_path, dev_path_size);

  ctx->dev_path = (LPWSTR) HeapAlloc(GetProcessHeap(), 0, dev_path_size);
  if (ctx->dev_path == NULL) {
    log_f("... failed to allocate memory for device path: %lu", GetLastError());
    return FALSE;
  }

  DEBUG_LOG("hid_scan_device(\"%ls\") => ctx->dev_path = %p", device_path, ctx->dev_path);

  memcpy(ctx->dev_path, device_path, dev_path_size);
  ctx->dev_path[dev_path_size - 1] = '\0';

  log_f("... DevicePath = %ls", ctx->dev_path);
  ctx->dev_handle = CreateFileW(
    ctx->dev_path,
    GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    NULL,
    OPEN_EXISTING,
    FILE_FLAG_OVERLAPPED,
    NULL);
  if (ctx->dev_handle == INVALID_HANDLE_VALUE) {
    error = GetLastError();
    length = FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER |
      FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS |
      FORMAT_MESSAGE_MAX_WIDTH_MASK,
      NULL,
      error,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPWSTR) &pBuffer,
      0,
      NULL);
    if (length) {
      // Remove space at the end
      pBuffer[length - 1] = '\0';

      log_f("... CreateFileW error: %lu (%ls)", error, pBuffer);
      LocalFree(pBuffer);
    } else {
      log_f("... CreateFileW error: %lu, FormatMessage error: %lu", error, GetLastError());
    }

    HeapFree(GetProcessHeap(), 0, ctx->dev_path);
    ctx->dev_path = NULL;
    ctx->dev_handle = INVALID_HANDLE_VALUE;

    return FALSE;
  }

  DEBUG_LOG("before HidD_GetPreparsedData");
  if (!HidD_GetPreparsedData(ctx->dev_handle, &ctx->pp_data)) {
    log_f("... HidD_GetPreparsedData error: %lu", GetLastError());
    goto end;
  }
  DEBUG_LOG("after HidD_GetPreparsedData");

  res = HidP_GetCaps(ctx->pp_data, &ctx->caps);
  if (res != HIDP_STATUS_SUCCESS) {
    log_f("... HidP_GetCaps error: 0x%08lx", res);
    goto end;
  }
  log_f("... Top-Level Usage: %u, Usage Page: 0x%04x",
    ctx->caps.Usage,
    ctx->caps.UsagePage);

  // 0xffca is the card reader usage page ID
  if (ctx->caps.UsagePage != CARD_READER_USAGE_PAGE) {
    log_f("... Incorrect usage page");
    goto end;
  } else if (ctx->caps.NumberInputValueCaps == 0) {
    log_f("... No value caps");
    goto end;
  }

  hid_print_caps(ctx);

  DEBUG_LOG("hid_scan_device(\"%ls\") => collection size: %Iu", ctx->dev_path, ctx->caps.NumberInputValueCaps * sizeof(HIDP_VALUE_CAPS));
  ctx->collection_length = ctx->caps.NumberInputValueCaps;
  ctx->collection = (HIDP_VALUE_CAPS *) HeapAlloc(GetProcessHeap(), 0, ctx->collection_length * sizeof(HIDP_VALUE_CAPS));
  if (ctx->collection == NULL) {
    log_f("... failed to allocate memory for HID Value Capabilities: %lu", GetLastError());
    goto end;
  }
  res = HidP_GetValueCaps(
    HidP_Input,
    ctx->collection,
    &ctx->collection_length,
    ctx->pp_data);
  if (res != HIDP_STATUS_SUCCESS) {
    log_f("... HidP_GetLinkCollectionNodes error: 0x%08lx", res);
    goto end;
  }

  for (i = 0; i < ctx->collection_length; i++) {
    HIDP_VALUE_CAPS *item = &ctx->collection[i];
    log_f("... collection[%Iu]", i);
    log_f("...   UsagePage = 0x%04x", item->UsagePage);
    log_f("...   ReportID = %u", item->ReportID);
    log_f("...   IsAlias = %u", item->IsAlias);
    log_f("...   LinkUsage = %u", item->LinkUsage);
    log_f("...   IsRange = %u", item->IsRange);
    log_f("...   IsAbsolute = %u", item->IsAbsolute);
    log_f("...   BitSize = %u", item->BitSize);
    log_f("...   ReportCount = %u", item->ReportCount);

    if (!item->IsRange) {
      log_f("...   collection[%Iu].NotRange:", i);
      log_f("...     Usage = 0x%x", item->NotRange.Usage);
      log_f("...     DataIndex = %u", item->NotRange.DataIndex);
    }
  }

  ctx->initialized = TRUE;

  return TRUE;

end:
  hid_ctx_reset(ctx);

  return FALSE;
}

/*
 * Checks all devices registered with the HIDClass GUID. If the usage page of
 * the device is 0xffca, then a compatible card reader was found.
 *
 * Usage 0x41 => ISO_15693
 * Usage 0x42 => ISO_18092 (FeliCa)
 */
BOOL hid_scan() {
  BOOL res = TRUE;
  SP_DEVINFO_DATA devinfo_data;
  SP_DEVICE_INTERFACE_DATA device_interface_data;
  SP_DEVICE_INTERFACE_DETAIL_DATA_W *device_interface_detail_data = NULL;
  HDEVINFO device_info_set = (HDEVINFO) INVALID_HANDLE_VALUE;
  GUID hid_guid;
  DWORD dwPropertyRegDataType;
  wchar_t szBuffer[1024];
  wchar_t szGuid[64] = { 0 };
  DWORD device_index = 0;
  DWORD dwSize = 0;

  size_t hid_devices = 0;

  HidD_GetHidGuid(&hid_guid);
  StringFromGUID2(&hid_guid, szGuid, 64);
  log_f("HID guid: %ls", szGuid);

  StringFromGUID2(&hidclass_guid, szGuid, 64);
  log_f("HIDClass guid: %ls", szGuid);

  // HID collection opening needs `DIGCF_DEVICEINTERFACE` and ignore
  // disconnected devices
  device_info_set = SetupDiGetClassDevs(&hid_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (device_info_set == INVALID_HANDLE_VALUE) {
    log_f("SetupDiGetClassDevs error: %lu", GetLastError());

    res = FALSE;
    goto end;
  }

  memset(&devinfo_data, 0, sizeof(SP_DEVINFO_DATA));
  devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);
  device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

  // `SetupDiEnumDeviceInterfaces` must come before `SetupDiEnumDeviceInfo`
  // else `SetupDiEnumDeviceInterfaces` will fail with error 259
  while (SetupDiEnumDeviceInterfaces(device_info_set, NULL, &hid_guid, device_index, &device_interface_data)) {
    log_f("device_index: %lu", device_index);

    // Get the required size
    if (SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, NULL, 0, &dwSize, NULL)) {
      log_f("... unexpected successful SetupDiGetDeviceInterfaceDetailW: %lu", GetLastError());
      goto cont;
    }

    device_interface_detail_data = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSize);
    if (device_interface_detail_data == NULL) {
      log_f("... failed to allocate memory of size %lu for device interface detail data: %lu", dwSize, GetLastError());
      goto cont;
    }

    device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, device_interface_detail_data, dwSize, NULL, NULL)) {
      log_f("... SetupDiGetDeviceInterfaceDetailW error: %lu", GetLastError());
      goto cont;
    }

    if (!SetupDiEnumDeviceInfo(device_info_set, device_index, &devinfo_data)) {
      log_f("... SetupDiEnumDeviceInfo error: %lu", GetLastError());
      goto cont;
    }

    StringFromGUID2(&devinfo_data.ClassGuid, szGuid, 64);
    log_f("... Class GUID: %ls", szGuid);

    if (!IsEqualGUID(&hidclass_guid, &devinfo_data.ClassGuid)) {
      log_f("... Incorrect Class GUID");
      goto cont;
    }

    if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_CLASS, NULL, (BYTE *) &szBuffer, sizeof(szBuffer), NULL)) {
      log_f("... Driver Name: %ls", szBuffer);
    }

    if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_DEVICEDESC, &dwPropertyRegDataType, (BYTE *) &szBuffer, sizeof(szBuffer), &dwSize)) {
      log_f("... Device Description: %ls", szBuffer);
    }

    EnterCriticalSection(&crit_section);

    if (hid_devices == contexts_length) {
      contexts_length++;

      contexts = (struct eamio_hid_device *) HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, contexts, contexts_length * sizeof(struct eamio_hid_device));
      if (contexts == NULL) {
        log_f("failed to reallocate memory for HID device information: %lu", GetLastError());

        LeaveCriticalSection(&crit_section);

        HeapFree(GetProcessHeap(), 0, device_interface_detail_data);

        res = FALSE;
        goto end;
      }

      hid_ctx_init(&contexts[hid_devices]);
    }

    if (hid_scan_device(&contexts[hid_devices], device_interface_detail_data->DevicePath)) {
      hid_devices++;
    }

    LeaveCriticalSection(&crit_section);

cont:
    if (device_interface_detail_data) {
      HeapFree(GetProcessHeap(), 0, device_interface_detail_data);
      device_interface_detail_data = NULL;
    }

    device_index++;
  }

end:
  if (device_info_set != INVALID_HANDLE_VALUE) {
    SetupDiDestroyDeviceInfoList(device_info_set);
  }

  return res;
}

hid_poll_value_t hid_device_poll(struct eamio_hid_device *ctx) {
  DWORD error = 0;

  if (!ctx->initialized) {
    return HID_POLL_ERROR;
  }

  if (ctx->io_pending) {
    // Do this if inside to not have more `ReadFile` overlapped I/O requests.
    // If there are more calls to `ReadFile` than `GetOverlappedResult` then
    // eventually the working set quota will run out triggering error 1426
    // (ERROR_WORKING_SET_QUOTA).
    if (HasOverlappedIoCompleted(&ctx->read_state)) {
      ctx->io_pending = FALSE;

      log_f("read finished");
      if (!GetOverlappedResult(ctx->dev_handle, &ctx->read_state, &ctx->read_size, FALSE)) {
        log_f("GetOverlappedResult error: %lu", GetLastError());
        return HID_POLL_ERROR;
      }

      memset(&ctx->read_state, 0, sizeof(OVERLAPPED));

      return HID_POLL_CARD_READY;
    }
  } else {
    if (!ReadFile(
      ctx->dev_handle,
      &ctx->report_buffer,
      sizeof(ctx->report_buffer),
      &ctx->read_size,
      &ctx->read_state))
    {
      error = GetLastError();

      if (error == ERROR_IO_PENDING) {
        ctx->io_pending = TRUE;
      } else {
        log_f("ReadFile error: %lu", error);
        return FALSE;
      }
    } else {
      // The read completed right away
      return HID_POLL_CARD_READY;
    }
  }

  return HID_POLL_CARD_NOT_READY;
}

static const char *hid_card_type_name(hid_card_type_t card_type) {
  switch (card_type) {
    case HID_CARD_NONE: return "none";
    case HID_CARD_ISO_15693: return "ISO 15693";
    case HID_CARD_ISO_18092: return "ISO 18092 (FeliCa)";
    default: return "unknown";
  }
}

uint8_t hid_device_read(struct eamio_hid_device *hid_ctx) {
  DWORD error = 0;
  NTSTATUS res;

  int i = 0;

  if (!hid_ctx->initialized) {
    return HID_CARD_NONE;
  }

  if (hid_ctx->io_pending) {
    return HID_CARD_NONE;
  }
  if (hid_ctx->read_size == 0) {
    log_f("not enough bytes read, dwSize = %lu", hid_ctx->read_size);
    return HID_CARD_NONE;
  }

  log_f("got report: %02x %02x %02x %02x %02x %02x %02x %02x %02x",
    hid_ctx->report_buffer[0],
    hid_ctx->report_buffer[1],
    hid_ctx->report_buffer[2],
    hid_ctx->report_buffer[3],
    hid_ctx->report_buffer[4],
    hid_ctx->report_buffer[5],
    hid_ctx->report_buffer[6],
    hid_ctx->report_buffer[7],
    hid_ctx->report_buffer[8]);

  for (i = 0; i < hid_ctx->collection_length; i++) {
    HIDP_VALUE_CAPS *item = &hid_ctx->collection[i];

    res = HidP_GetUsageValueArray(
      HidP_Input,
      CARD_READER_USAGE_PAGE,
      0, // LinkCollection
      item->NotRange.Usage,
      (PCHAR) &hid_ctx->usage_value,
      sizeof(hid_ctx->usage_value),
      hid_ctx->pp_data,
      (PCHAR) &hid_ctx->report_buffer,
      hid_ctx->read_size);

    // Loop through the collection to find the entry that handles
    // this ReportID
    if (res == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {
      continue;
    }

    if (res != HIDP_STATUS_SUCCESS) {
      log_f("HidP_GetData error: 0x%08lx", res);
      return HID_CARD_NONE;
    }

    log_f("Loaded card ID [%02X%02X%02X%02X%02X%02X%02X%02X] type %s (0x%02x)",
      hid_ctx->usage_value[0],
      hid_ctx->usage_value[1],
      hid_ctx->usage_value[2],
      hid_ctx->usage_value[3],
      hid_ctx->usage_value[4],
      hid_ctx->usage_value[5],
      hid_ctx->usage_value[6],
      hid_ctx->usage_value[7],
      hid_card_type_name(item->NotRange.Usage),
      item->NotRange.Usage);

    return item->NotRange.Usage;
  }
}

