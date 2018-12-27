#include <windows.h>
#include <stdio.h>
#include <strsafe.h>
#include <dbt.h>
#include <hidsdi.h>

#include "hid.h"
#include "log.h"
#include "window.h"

BOOL run_message_pump = TRUE;

BOOL RegisterGuid(HWND hWnd, HDEVNOTIFY *hDeviceNotify) {
  DEV_BROADCAST_DEVICEINTERFACE notification_filter;

  memset(&notification_filter, 0, sizeof(DEV_BROADCAST_DEVICEINTERFACE));
  notification_filter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
  notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

  HidD_GetHidGuid(&notification_filter.dbcc_classguid);

  *hDeviceNotify = RegisterDeviceNotificationW(hWnd, &notification_filter, DEVICE_NOTIFY_WINDOW_HANDLE);
  if (*hDeviceNotify = NULL) {
    log_f("RegisterDeviceNotification error: %lu", GetLastError());
    return FALSE;
  }
  log_f("RegisterDeviceNotification successful");

  return TRUE;
}

INT_PTR WINAPI WinProcCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  static HDEVNOTIFY hDeviceNotify;

  LRESULT lRet = 1;

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
          log_f("Message: WM_DEVICECHANGE message received, value %Iu unhandled", wParam);
          break;
      }
#endif

      if (lParam && (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)) {
        PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR) lParam;

        switch (pHdr->dbch_devicetype) {
          case DBT_DEVTYP_DEVICEINTERFACE:
          {
            PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE) pHdr;
            log_f(" -> DBT_DEVTYP_DEVICEINTERFACE => name: %ls", pDevInf->dbcc_name);

            if (wParam == DBT_DEVICEARRIVAL && hid_add_device(pDevInf->dbcc_name)) {
              log_f("HID reader found");
            } else {
              hid_remove_device(pDevInf->dbcc_name);
            }

            hid_print_contexts();

            break;
          }
#ifdef DBT_DEBUG
          case DBT_DEVTYP_HANDLE:
            log_f(" -> DBT_DEVTYP_HANDLE");
            break;
          case DBT_DEVTYP_OEM:
            log_f(" -> DBT_DEVTYP_OEM");
            break;
          case DBT_DEVTYP_PORT:
            log_f(" -> DBT_DEVTYP_PORT");
            break;
          case DBT_DEVTYP_VOLUME:
            log_f(" -> DBT_DEVTYP_VOLUME");
            break;
        }
#endif
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
      TEXT("cardio"),
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
