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

// include DEVPKEY_Device_BusReportedDeviceDesc from WinDDK\7600.16385.1\inc\api\devpropdef.h
#ifdef DEFINE_DEVPROPKEY
#undef DEFINE_DEVPROPKEY
#endif
#ifdef INITGUID
#define DEFINE_DEVPROPKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) const DEVPROPKEY DECLSPEC_SELECTANY name = { { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }, pid }
#else
#define DEFINE_DEVPROPKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) const DEVPROPKEY name
#endif // INITGUID

// include DEVPKEY_Device_BusReportedDeviceDesc from WinDDK\7600.16385.1\inc\api\devpkey.h
DEFINE_DEVPROPKEY(DEVPKEY_Device_BusReportedDeviceDesc,  0x540b947e, 0x8b40, 0x45bc, 0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2, 4);     // DEVPROP_TYPE_STRING
DEFINE_DEVPROPKEY(DEVPKEY_Device_ContainerId,            0x8c7ed206, 0x3f8a, 0x4827, 0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c, 2);     // DEVPROP_TYPE_GUID
DEFINE_DEVPROPKEY(DEVPKEY_Device_FriendlyName,           0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);    // DEVPROP_TYPE_STRING
DEFINE_DEVPROPKEY(DEVPKEY_DeviceDisplay_Category,        0x78c34fc8, 0x104a, 0x4aca, 0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57, 0x5a);  // DEVPROP_TYPE_STRING_LIST
DEFINE_DEVPROPKEY(DEVPKEY_Device_LocationInfo,           0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 15);    // DEVPROP_TYPE_STRING
DEFINE_DEVPROPKEY(DEVPKEY_Device_Manufacturer,           0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 13);    // DEVPROP_TYPE_STRING
DEFINE_DEVPROPKEY(DEVPKEY_Device_SecuritySDS,            0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 26);    // DEVPROP_TYPE_SECURITY_DESCRIPTOR_STRING

typedef BOOL (WINAPI *FN_SetupDiGetDevicePropertyW)(
  HDEVINFO DeviceInfoSet,
  PSP_DEVINFO_DATA DeviceInfoData,
  const DEVPROPKEY *PropertyKey,
  DEVPROPTYPE *PropertyType,
  PBYTE PropertyBuffer,
  DWORD PropertyBufferSize,
  PDWORD RequiredSize,
  DWORD Flags
);

void scan_for_device() {
  SP_DEVINFO_DATA devinfo_data;
  SP_DEVICE_INTERFACE_DATA device_interface_data;
  SP_DEVICE_INTERFACE_DETAIL_DATA_W *device_interface_detail_data = NULL;
  HDEVINFO device_info_set = (HDEVINFO) INVALID_HANDLE_VALUE;
  PHIDP_PREPARSED_DATA pp_data = NULL;
  HIDP_CAPS caps;
  GUID hid_guid;
  DWORD dwPropertyRegDataType;
  DEVPROPTYPE ulPropertyType;
  HANDLE dev_handle;
  WCHAR szDesc[2048];
  WCHAR szBuffer[2048];
  WCHAR driver_name[256];
  WCHAR szGuid[64] = { 0 };
  DWORD device_index = 0;
  DWORD dwSize = 0;
  NTSTATUS res;

  HidD_GetHidGuid(&hid_guid);
  StringFromGUID2(&hid_guid, szGuid, 64);
  log_f("HID guid: %ls", szGuid);

  StringFromGUID2(&hidclass_guid, szGuid, 64);
  log_f("HIDClass guid: %ls", szGuid);

  FN_SetupDiGetDevicePropertyW SetupDiGetDevicePropertyW = (FN_SetupDiGetDevicePropertyW) GetProcAddress(GetModuleHandle("setupapi.dll"), "SetupDiGetDevicePropertyW");

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

    if (SetupDiGetDevicePropertyW(device_info_set, &devinfo_data, &DEVPKEY_Device_Manufacturer, &ulPropertyType, (BYTE *) &szBuffer, sizeof(szBuffer), &dwSize, 0)) {
      log_f("i: %lu, Manufacturer: %ls", device_index, szBuffer);
    }

    dev_handle = CreateFileW(
      device_interface_detail_data->DevicePath,
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      NULL,
      OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED,
      0);
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
      log_f("i: %lu, HidP_GetCaps error: %lu", device_index, res);
      goto cont_close;
    }
    log_f("i: %lu, Usage: %u, Usage Page: 0x%04x", device_index, caps.Usage, caps.UsagePage);

    HidD_FreePreparsedData(pp_data);
cont_close:
    CloseHandle(dev_handle);
cont:
    if (device_interface_detail_data) {
      free(device_interface_detail_data);
    }

    device_index++;
  }
  log_f("i: %lu, SetupDiEnumDeviceInterfaces error: %lu", device_index, GetLastError());

end:
  if (device_info_set != INVALID_HANDLE_VALUE) {
    SetupDiDestroyDeviceInfoList(device_info_set);
  }
}
