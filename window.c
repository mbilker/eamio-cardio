#include <windows.h>
#include <stdio.h>
#include <strsafe.h>
#include <dbt.h>

#include "drive_check.h"
#include "window.h"

// Windows CE USB ActiveSync USB PnP GUID - Works for most USB devices
//GUID target_guid = { 0x25dbce51, 0x6c8f, 0x4a72, 0x8a, 0x6d, 0xb5, 0x4c, 0x2b, 0x4f, 0xc8, 0x35 };

// GUID_DEVINTERFACE_USB_DEVICE
GUID target_guid = { 0xa5dcbf10, 0x6530, 0x11d2, 0x90, 0x1f, 0x00, 0xc0, 0x4f, 0xb9, 0x51, 0xed };

BOOL run_message_pump = TRUE;
DWORD message_pump_thread_id = -1;

static void log_default(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

log_func_t log_f = log_default;

void set_log_func(log_func_t log_func) {
  log_f = log_func;
}

char FirstDriveFromMask(ULONG unit_mask) {
  char i;

  for (i = 0; i < 26; i++) {
    if (unit_mask & 0x1) {
      break;
    }
    unit_mask >>= 1;
  }

  return ('A' + i);
}

BOOL RegisterGuid(HWND hWnd, HDEVNOTIFY *hDeviceNotify) {
  DEV_BROADCAST_DEVICEINTERFACE notification_filter;

  memset(&notification_filter, 0, sizeof(DEV_BROADCAST_DEVICEINTERFACE));
  notification_filter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
  notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
  notification_filter.dbcc_classguid = target_guid;

  *hDeviceNotify = RegisterDeviceNotification(hWnd, &notification_filter, DEVICE_NOTIFY_WINDOW_HANDLE);

  if (*hDeviceNotify = NULL) {
    log_f("RegisterDeviceNotification error: %lu", GetLastError());
    return FALSE;
  }
  log_f("RegisterDeviceNotification successful");

  return TRUE;
}

INT_PTR WINAPI WinProcCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  LRESULT lRet = 1;
  static HDEVNOTIFY hDeviceNotify;

  switch (message) {
    case WM_CREATE:
      if (!RegisterGuid(hWnd, &hDeviceNotify)) {
        log_f("RegisterGuid error: %lu", GetLastError());
        lRet = 0;
        return lRet;
      }
      log_f("RegisterGuid successful");
      break;

    case WM_CLOSE:
      if (!UnregisterDeviceNotification(hDeviceNotify)) {
        DWORD error = GetLastError();

        // The hanlde may be invalid by this point
        if (error != ERROR_INVALID_HANDLE) {
          log_f("UnregisterDeviceNotification error: %lu", error);
        }
      } else {
        log_f("UnregisterDeviceNotification successful");
      }
      DestroyWindow(hWnd);
      break;

    case WM_DESTROY:
      PostQuitMessage(0);
      break;

    case WM_DEVICECHANGE:
    {
#ifdef DBT_DEBUG
      switch (wParam) {
        case DBT_DEVICEARRIVAL:
          log_f("Message: DBT_DEVICEARRIVAL");
          break;
        case DBT_DEVICEREMOVECOMPLETE:
          log_f("Message: DBT_DEVICEREMOVECOMPLETE");
          break;
        case DBT_DEVNODES_CHANGED:
          // Don't care
          break;
        default:
          log_f("Message: WM_DEVICECHANGE message received, value %u unhandled", wParam);
          break;
      }
#endif

      if (lParam && (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)) {
        PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR) lParam;

        switch (pHdr->dbch_devicetype) {
#ifdef DBT_DEBUG
          case DBT_DEVTYP_DEVICEINTERFACE:
          {
            PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE) pHdr;
            log_f(" -> DBT_DEVTYP_DEVICEINTERFACE => name: %s", pDevInf->dbcc_name);
            break;
          }
          case DBT_DEVTYP_HANDLE:
            log_f(" -> DBT_DEVTYP_HANDLE");
            break;
          case DBT_DEVTYP_OEM:
            log_f(" -> DBT_DEVTYP_OEM");
            break;
          case DBT_DEVTYP_PORT:
            log_f(" -> DBT_DEVTYP_PORT");
            break;
#endif
          case DBT_DEVTYP_VOLUME:
          {
            PDEV_BROADCAST_VOLUME pDevVolume = (PDEV_BROADCAST_VOLUME) pHdr;
            char drive_letter = FirstDriveFromMask(pDevVolume->dbcv_unitmask);
            log_f(" -> DBT_DEVTYP_VOLUME => Drive %c %s",
              drive_letter,
              (wParam == DBT_DEVICEARRIVAL) ? "has arrived" : "was removed");

            if (wParam == DBT_DEVICEARRIVAL) {
              check_for_file(drive_letter);
            }

            break;
          }
        }
      }
    }

    default:
      lRet = DefWindowProc(hWnd, message, wParam, lParam);
      break;
  }

  return lRet;
}

BOOL InitWindowClass() {
  WNDCLASSEX wnd_class;

  wnd_class.cbSize = sizeof(WNDCLASSEX);
  wnd_class.style = CS_OWNDC;
  wnd_class.hInstance = GetModuleHandle(0);
  wnd_class.lpfnWndProc = (WNDPROC) WinProcCallback;
  wnd_class.cbClsExtra = 0;
  wnd_class.cbWndExtra = 0;
  wnd_class.hIcon = NULL;
  wnd_class.hbrBackground = NULL;
  wnd_class.hCursor = NULL;
  wnd_class.lpszClassName = WND_CLASS_NAME;
  wnd_class.lpszMenuName = NULL;
  wnd_class.hIconSm = NULL;

  if (!RegisterClassEx(&wnd_class)) {
    log_f("RegisterClassEx error: %lu", GetLastError());
    return FALSE;
  }
  log_f("RegisterClassEx successful");

  return TRUE;
}

HWND CreateTheWindow(HINSTANCE hInstance) {
  HWND hWnd = CreateWindowEx(
      0,
      WND_CLASS_NAME,
      "cardio",
      WS_DISABLED,
      0, 0,
      CW_USEDEFAULT, CW_USEDEFAULT,
      NULL,
      NULL,
      hInstance,
      NULL);

  if (hWnd == NULL) {
    log_f("CreateWindowEx error: %lu", GetLastError());
    return NULL;
  }
  log_f("CreateWindowEx successful");

  return hWnd;
}

BOOL MessagePump(HWND hWnd) {
  MSG msg;
  int ret_val;

  while (run_message_pump && ((ret_val = GetMessage(&msg, hWnd, 0, 0)) != 0)) {
    if (ret_val == -1) {
      log_f("GetMessage error: %lu", GetLastError());
      return FALSE;
    } else {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return TRUE;
}

BOOL EndTheWindow(HWND hWnd) {
  run_message_pump = FALSE;

  return PostMessage(hWnd, WM_CLOSE, 0, 0);
}
