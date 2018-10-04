#include <windows.h>
#include <devguid.h>
#include <hidclass.h>
#include <hidusage.h>
#include <hidpi.h>
#include <hidsdi.h>
#include <setupapi.h>

#include "drive_check.h"
#include "hid.h"
#include "window.h"

// GUID_DEVINTERFACE_HID
//GUID class_interface_guid = { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

// GUID_DEVCLASS_HIDCLASS
GUID hidclass_guid = { 0x745a17a0, 0x74d3, 0x11d0, { 0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda } };

void scan_for_device() {
  SP_DEVINFO_DATA devinfo_data;
  SP_DEVICE_INTERFACE_DATA device_interface_data;
  SP_DEVICE_INTERFACE_DETAIL_DATA_W *device_interface_detail_data = NULL;
  HDEVINFO device_info_set = (HDEVINFO) INVALID_HANDLE_VALUE;
  PHIDP_PREPARSED_DATA pp_data = NULL;
  HIDP_CAPS caps;
  HIDP_VALUE_CAPS *collection = NULL;
  GUID hid_guid;
  DWORD dwPropertyRegDataType;
  DEVPROPTYPE ulPropertyType;
  HANDLE dev_handle = INVALID_HANDLE_VALUE;
  WCHAR szDesc[2048];
  WCHAR szBuffer[2048];
  WCHAR driver_name[256];
  WCHAR szGuid[64] = { 0 };
  DWORD device_index = 0;
  BYTE report_buffer[128] = { 0 };
  unsigned char usage_value[128] = { 0 };
  DWORD error = 0;
  DWORD dwSize = 0;
  USHORT collection_length = 0;
  NTSTATUS res;
  OVERLAPPED overlap_state;
  BOOL have_current_io_request = FALSE;
  BYTE report_id = -1;
  ULONG expected_report_size = -1;

  int i = 0, j = 0;

  HidD_GetHidGuid(&hid_guid);
  StringFromGUID2(&hid_guid, szGuid, 64);
  log_f("HID guid: %ls", szGuid);

  StringFromGUID2(&hidclass_guid, szGuid, 64);
  log_f("HIDClass guid: %ls", szGuid);

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
    if (device_index > 0) {
      log_f("");
    }

    // Get the required size
    if (SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, NULL, 0, &dwSize, NULL)) {
      log_f("i: %lu, unexpected successful SetupDiGetDeviceInterfaceDetailW: %lu", device_index, GetLastError());
      goto cont;
    }

    device_interface_detail_data = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *) malloc(dwSize);
    if (device_interface_detail_data == NULL) {
      log_f("i: %lu, device_interface_detail_data malloc(%lu) failed: %lu", device_index, dwSize, GetLastError());
      goto cont;
    }

    device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, device_interface_detail_data, dwSize, NULL, NULL)) {
      log_f("i: %lu, SetupDiGetDeviceInterfaceDetailW error: %lu", device_index, GetLastError());
      goto cont;
    }

    if (!SetupDiEnumDeviceInfo(device_info_set, device_index, &devinfo_data)) {
      log_f("i: %lu, SetupDiEnumDeviceInfo error: %lu", device_index, GetLastError());
      goto cont;
    }

    StringFromGUID2(&devinfo_data.ClassGuid, szGuid, 64);
    log_f("i: %lu, guid: %ls", device_index, szGuid);

    if (!IsEqualGUID(&hidclass_guid, &devinfo_data.ClassGuid)) {
      log_f("i: %lu, incorrect class GUID", device_index);
      goto cont;
    }

    if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_CLASS, NULL, (BYTE *) &driver_name, sizeof(driver_name), NULL)) {
      log_f("i: %lu, Driver Name: %ls", device_index, driver_name);
    }

    if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_DEVICEDESC, &dwPropertyRegDataType, (BYTE *) &szDesc, sizeof(szDesc), &dwSize)) {
      log_f("i: %lu, Device Description: %ls", device_index, szDesc);
    }

    log_f("i: %lu, DevicePath = %ls", device_index, device_interface_detail_data->DevicePath);

    dev_handle = CreateFileW(
      device_interface_detail_data->DevicePath,
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      NULL,
      OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED,
      NULL);
    if (dev_handle == INVALID_HANDLE_VALUE) {
      log_f("i: %lu, CreateFileW error: %lu", device_index, GetLastError());
      goto cont;
    }
    if (!HidD_GetPreparsedData(dev_handle, &pp_data)) {
      log_f("i: %lu, HidD_GetPreparsedData error: %lu", device_index, GetLastError());
      goto cont_close;
    }

    res = HidP_GetCaps(pp_data, &caps);
    if (res != HIDP_STATUS_SUCCESS) {
      log_f("i: %lu, HidP_GetCaps error: 0x%08lx", device_index, res);
      goto cont_close;
    }
    log_f("i: %lu, Top-Level Collection Usage: %u, Usage Page: 0x%04x", device_index, caps.Usage, caps.UsagePage);

    // 0xffca is the card reader usage page ID
    if (caps.UsagePage != 0xffca) {
      log_f("i: %lu, incorrect usage page", device_index);
      goto cont_hid;
    } else if (caps.NumberInputValueCaps == 0) {
      log_f("i: %lu, no value caps", device_index);
      goto cont_hid;
    }

#define VALUE(KEY) log_f("i: %lu, " #KEY ": %u", device_index, KEY)

    VALUE(caps.InputReportByteLength);
    VALUE(caps.OutputReportByteLength);
    VALUE(caps.FeatureReportByteLength);
    VALUE(caps.NumberLinkCollectionNodes);
    VALUE(caps.NumberInputButtonCaps);
    VALUE(caps.NumberInputValueCaps);
    VALUE(caps.NumberInputDataIndices);
    VALUE(caps.NumberOutputButtonCaps);
    VALUE(caps.NumberOutputValueCaps);
    VALUE(caps.NumberOutputDataIndices);
    VALUE(caps.NumberFeatureButtonCaps);
    VALUE(caps.NumberFeatureValueCaps);
    VALUE(caps.NumberFeatureDataIndices);

#undef VALUE

    collection_length = caps.NumberInputValueCaps;
    collection = (HIDP_VALUE_CAPS *) malloc(collection_length * sizeof(HIDP_VALUE_CAPS));
    res = HidP_GetValueCaps(HidP_Input, collection, &collection_length, pp_data);
    if (res != HIDP_STATUS_SUCCESS) {
      log_f("i: %lu, HidP_GetLinkCollectionNodes error: 0x%08lx", device_index, res);
      goto cont_hid;
    }

    for (i = 0; i < collection_length; i++) {
      log_f("device_index: %lu", device_index);
      log_f("  collection[%d]", i);
      log_f("    UsagePage = 0x%04x", collection[i].UsagePage);
      log_f("    ReportID = %u", collection[i].ReportID);
      log_f("    IsAlias = %u", collection[i].IsAlias);
      log_f("    LinkUsage = %u", collection[i].LinkUsage);
      log_f("    IsRange = %u", collection[i].IsRange);
      log_f("    IsAbsolute = %u", collection[i].IsAbsolute);
      log_f("    BitSize = %u", collection[i].BitSize);
      log_f("    ReportCount = %u", collection[i].ReportCount);

      if (!collection[i].IsRange) {
        log_f("    collection[%d].NotRange:", i);
        log_f("      Usage = 0x%x", collection[i].NotRange.Usage);
        log_f("      DataIndex = %u", collection[i].NotRange.DataIndex);
      }
    }

    memset(&overlap_state, 0, sizeof(OVERLAPPED));
    i = 0;

    while (1) {
      if (have_current_io_request) {
        if (HasOverlappedIoCompleted(&overlap_state)) {
          have_current_io_request = FALSE;

          log_f("read finished");
          if (!GetOverlappedResult(dev_handle, &overlap_state, &dwSize, FALSE)) {
            log_f("i: %lu, GetOverlappedResult error: %lu", device_index, GetLastError());
            goto cont_hid;
          }

          memset(&overlap_state, 0, sizeof(OVERLAPPED));
        } else {
          // Give Windows some time to do other things
          Sleep(16);
          continue;
        }
      } else if (!ReadFile(dev_handle, &report_buffer, sizeof(report_buffer), &dwSize, &overlap_state)) {
        error = GetLastError();

        if (error == ERROR_IO_PENDING) {
          have_current_io_request = TRUE;
        } else {
          log_f("i: %lu, ReadFile error: %lu", device_index, error);
          goto cont_hid;
        }

        continue;
      }

      if (dwSize == 0) {
        log_f("not enough bytes read, dwSize = %lu", dwSize);
        continue;
      }

      log_f("got report: %02x %02x %02x %02x %02x %02x %02x %02x %02x",
        report_buffer[0],
        report_buffer[1],
        report_buffer[2],
        report_buffer[3],
        report_buffer[4],
        report_buffer[5],
        report_buffer[6],
        report_buffer[7],
        report_buffer[8]);

      for (j = 0; j < collection_length; j++) {
        res = HidP_GetUsageValueArray(
          HidP_Input,
          caps.UsagePage,
          0,
          collection[j].NotRange.Usage,
          (PCHAR) &usage_value,
          sizeof(usage_value),
          pp_data,
          (PCHAR) &report_buffer,
          dwSize);

        // Loop through the collection to find the entry that handles
        // this ReportID
        if (res == HIDP_STATUS_INCOMPATIBLE_REPORT_ID) {
          continue;
        }

        if (res != HIDP_STATUS_SUCCESS) {
          log_f("i: %lu, HidP_GetData error: 0x%08lx", device_index, res);

          const char *msg;
#define B(MSG) case MSG: msg = #MSG; break
          switch (res) {
            B(HIDP_STATUS_INVALID_PREPARSED_DATA);
            B(HIDP_STATUS_INVALID_REPORT_TYPE);
            B(HIDP_STATUS_INVALID_REPORT_LENGTH);
            B(HIDP_STATUS_USAGE_NOT_FOUND);
            B(HIDP_STATUS_VALUE_OUT_OF_RANGE);
            B(HIDP_STATUS_BAD_LOG_PHY_VALUES);
            B(HIDP_STATUS_BUFFER_TOO_SMALL);
            B(HIDP_STATUS_INTERNAL_ERROR);
            B(HIDP_STATUS_INCOMPATIBLE_REPORT_ID);
            B(HIDP_STATUS_NOT_VALUE_ARRAY);
            B(HIDP_STATUS_IS_VALUE_ARRAY);
            B(HIDP_STATUS_DATA_INDEX_NOT_FOUND);
            B(HIDP_STATUS_DATA_INDEX_OUT_OF_RANGE);
            default: msg = "unknown";
          }
#undef B
          log_f("error type: %s", msg);
          goto cont_hid;
        }

        log_f("got report %02x: %02x %02x %02x %02x %02x %02x %02x %02x",
          collection[j].NotRange.Usage,
          usage_value[0],
          usage_value[1],
          usage_value[2],
          usage_value[3],
          usage_value[4],
          usage_value[5],
          usage_value[6],
          usage_value[7]);
      }

      Sleep(2000);

      if (i++ >= 5) {
        break;
      }
    }

cont_hid:
    if (collection != NULL) {
      free(collection);
      collection = NULL;
    }

    HidD_FreePreparsedData(pp_data);
    pp_data = NULL;
cont_close:
    CloseHandle(dev_handle);
    dev_handle = NULL;
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
}
