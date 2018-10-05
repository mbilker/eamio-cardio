#include <windows.h>
#include <devguid.h>
#include <hidclass.h>
#include <hidusage.h>
#include <hidpi.h>
#include <hidsdi.h>
#include <setupapi.h>

#include "hid.h"
#include "log.h"

// GUID_DEVINTERFACE_HID
//GUID class_interface_guid = { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

// GUID_DEVCLASS_HIDCLASS
GUID hidclass_guid = { 0x745a17a0, 0x74d3, 0x11d0, { 0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda } };

void hid_ctx_init(struct eamio_hid_device *hid_ctx) {
  hid_ctx->dev_handle = INVALID_HANDLE_VALUE;
  hid_ctx->initialized = FALSE;
  hid_ctx->io_pending = FALSE;
  hid_ctx->read_size = 0;
  hid_ctx->pp_data = NULL;
  hid_ctx->collection = NULL;
  hid_ctx->collection_length = 0;

  memset(&hid_ctx->read_state, 0, sizeof(OVERLAPPED));
  memset(&hid_ctx->report_buffer, 0, sizeof(hid_ctx->report_buffer));
  memset(&hid_ctx->usage_value, 0, sizeof(hid_ctx->usage_value));
  memset(&hid_ctx->caps, 0, sizeof(HIDP_CAPS));
}

void hid_free(struct eamio_hid_device *hid_ctx) {
  if (hid_ctx->dev_handle != INVALID_HANDLE_VALUE) {
    CancelIo(hid_ctx->dev_handle);

    CloseHandle(hid_ctx->dev_handle);
    hid_ctx->dev_handle = INVALID_HANDLE_VALUE;
  }

  if (hid_ctx->pp_data != NULL) {
    HidD_FreePreparsedData(hid_ctx->pp_data);
    hid_ctx->pp_data = NULL;
  }

  if (hid_ctx->collection != NULL) {
    free(hid_ctx->collection);
    hid_ctx->collection = NULL;
  }
}

void hid_print_caps(struct eamio_hid_device *hid_ctx) {
#define VALUE(KEY) log_f(#KEY ": %u", KEY)
  VALUE(hid_ctx->caps.InputReportByteLength);
  VALUE(hid_ctx->caps.OutputReportByteLength);
  VALUE(hid_ctx->caps.FeatureReportByteLength);
  VALUE(hid_ctx->caps.NumberLinkCollectionNodes);
  VALUE(hid_ctx->caps.NumberInputButtonCaps);
  VALUE(hid_ctx->caps.NumberInputValueCaps);
  VALUE(hid_ctx->caps.NumberInputDataIndices);
  VALUE(hid_ctx->caps.NumberOutputButtonCaps);
  VALUE(hid_ctx->caps.NumberOutputValueCaps);
  VALUE(hid_ctx->caps.NumberOutputDataIndices);
  VALUE(hid_ctx->caps.NumberFeatureButtonCaps);
  VALUE(hid_ctx->caps.NumberFeatureValueCaps);
  VALUE(hid_ctx->caps.NumberFeatureDataIndices);
#undef VALUE
}

/*
 * Checks all devices registered with the HIDClass GUID. If the usage page of
 * the device is 0xffca, then a compatible card reader was found.
 *
 * Usage 0x41 => ISO_15693
 * Usage 0x42 => ISO_18092 (FeliCa)
 */
BOOL hid_scan(struct eamio_hid_device *hid_ctx) {
  SP_DEVINFO_DATA devinfo_data;
  SP_DEVICE_INTERFACE_DATA device_interface_data;
  SP_DEVICE_INTERFACE_DETAIL_DATA_W *device_interface_detail_data = NULL;
  HDEVINFO device_info_set = (HDEVINFO) INVALID_HANDLE_VALUE;
  GUID hid_guid;
  DWORD dwPropertyRegDataType;
  DEVPROPTYPE ulPropertyType;
  WCHAR szDesc[2048];
  WCHAR szBuffer[2048];
  WCHAR driver_name[256];
  WCHAR szGuid[64] = { 0 };
  DWORD device_index = 0;
  DWORD dwSize = 0;
  NTSTATUS res;

  int i = 0;

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
      log_f("  unexpected successful SetupDiGetDeviceInterfaceDetailW: %lu", GetLastError());
      goto cont;
    }

    device_interface_detail_data = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *) malloc(dwSize);
    if (device_interface_detail_data == NULL) {
      log_f("  device_interface_detail_data malloc(%lu) failed: %lu", dwSize, GetLastError());
      goto cont;
    }

    device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, device_interface_detail_data, dwSize, NULL, NULL)) {
      log_f("  SetupDiGetDeviceInterfaceDetailW error: %lu", GetLastError());
      goto cont;
    }

    if (!SetupDiEnumDeviceInfo(device_info_set, device_index, &devinfo_data)) {
      log_f("  SetupDiEnumDeviceInfo error: %lu", GetLastError());
      goto cont;
    }

    StringFromGUID2(&devinfo_data.ClassGuid, szGuid, 64);
    log_f("  Class GUID: %ls", szGuid);

    if (!IsEqualGUID(&hidclass_guid, &devinfo_data.ClassGuid)) {
      log_f("  Incorrect Class GUID");
      goto cont;
    }

    if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_CLASS, NULL, (BYTE *) &driver_name, sizeof(driver_name), NULL)) {
      log_f("  Driver Name: %ls", driver_name);
    }

    if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_DEVICEDESC, &dwPropertyRegDataType, (BYTE *) &szDesc, sizeof(szDesc), &dwSize)) {
      log_f("  Device Description: %ls", szDesc);
    }

    log_f("  DevicePath = %ls", device_interface_detail_data->DevicePath);
    hid_ctx->dev_handle = CreateFileW(
      device_interface_detail_data->DevicePath,
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      NULL,
      OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED,
      NULL);
    if (hid_ctx->dev_handle == INVALID_HANDLE_VALUE) {
      log_f("  CreateFileW error: %lu", GetLastError());
      goto cont;
    }
    if (!HidD_GetPreparsedData(hid_ctx->dev_handle, &hid_ctx->pp_data)) {
      log_f("  HidD_GetPreparsedData error: %lu", GetLastError());
      goto cont_hid;
    }

    res = HidP_GetCaps(hid_ctx->pp_data, &hid_ctx->caps);
    if (res != HIDP_STATUS_SUCCESS) {
      log_f("  HidP_GetCaps error: 0x%08lx", res);
      goto cont_hid;
    }
    log_f("  Top-Level Usage: %u, Usage Page: 0x%04x",
      hid_ctx->caps.Usage,
      hid_ctx->caps.UsagePage);

    // 0xffca is the card reader usage page ID
    if (hid_ctx->caps.UsagePage != 0xffca) {
      log_f("  Incorrect usage page");
      goto cont_hid;
    } else if (hid_ctx->caps.NumberInputValueCaps == 0) {
      log_f("  No value caps");
      goto cont_hid;
    }

    hid_print_caps(hid_ctx);

    hid_ctx->collection_length = hid_ctx->caps.NumberInputValueCaps;
    hid_ctx->collection = (HIDP_VALUE_CAPS *) malloc(hid_ctx->collection_length * sizeof(HIDP_VALUE_CAPS));
    res = HidP_GetValueCaps(
      HidP_Input,
      hid_ctx->collection,
      &hid_ctx->collection_length,
      hid_ctx->pp_data);
    if (res != HIDP_STATUS_SUCCESS) {
      log_f("  HidP_GetLinkCollectionNodes error: 0x%08lx", res);
      goto cont_hid;
    }

    for (i = 0; i < hid_ctx->collection_length; i++) {
      HIDP_VALUE_CAPS *item = &hid_ctx->collection[i];
      log_f("  collection[%d]", i);
      log_f("    UsagePage = 0x%04x", item->UsagePage);
      log_f("    ReportID = %u", item->ReportID);
      log_f("    IsAlias = %u", item->IsAlias);
      log_f("    LinkUsage = %u", item->LinkUsage);
      log_f("    IsRange = %u", item->IsRange);
      log_f("    IsAbsolute = %u", item->IsAbsolute);
      log_f("    BitSize = %u", item->BitSize);
      log_f("    ReportCount = %u", item->ReportCount);

      if (!item->IsRange) {
        log_f("    collection[%d].NotRange:", i);
        log_f("      Usage = 0x%x", item->NotRange.Usage);
        log_f("      DataIndex = %u", item->NotRange.DataIndex);
      }
    }

    hid_ctx->initialized = TRUE;

    free(device_interface_detail_data);
    device_interface_detail_data = NULL;

    SetupDiDestroyDeviceInfoList(device_info_set);

    return TRUE;

cont_hid:
    hid_free(hid_ctx);

cont:
    if (device_interface_detail_data) {
      free(device_interface_detail_data);
      device_interface_detail_data = NULL;
    }

    device_index++;
  }
  log_f("i: %lu, SetupDiEnumDeviceInterfaces error: %lu", device_index, GetLastError());

end:
  if (device_info_set != INVALID_HANDLE_VALUE) {
    SetupDiDestroyDeviceInfoList(device_info_set);
  }

  return FALSE;
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
      hid_ctx->caps.UsagePage,
      0,
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

    log_f("got report %02x: %02x %02x %02x %02x %02x %02x %02x %02x",
      item->NotRange.Usage,
      hid_ctx->usage_value[0],
      hid_ctx->usage_value[1],
      hid_ctx->usage_value[2],
      hid_ctx->usage_value[3],
      hid_ctx->usage_value[4],
      hid_ctx->usage_value[5],
      hid_ctx->usage_value[6],
      hid_ctx->usage_value[7]);

    return item->NotRange.Usage;
  }
}

