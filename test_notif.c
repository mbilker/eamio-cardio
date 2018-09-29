#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <strsafe.h>
#include <dbt.h>

#define WND_CLASS_NAME TEXT("cardio Notification Window")

// Windows CE USB ActiveSync USB PnP GUID - Works for most USB devices
//GUID target_guid = { 0x25dbce51, 0x6c8f, 0x4a72, 0x8a, 0x6d, 0xb5, 0x4c, 0x2b, 0x4f, 0xc8, 0x35 };

//GUID target_guid = { 0x4d36e967, 0xe325, 0x11ce, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 };

// GUID_DEVINTERFACE_USB_DEVICE
GUID target_guid = { 0xa5dcbf10, 0x6530, 0x11d2, 0x90, 0x1f, 0x00, 0xc0, 0x4f, 0xb9, 0x51, 0xed };

uint8_t ID[2][8];

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

char hexval(char c) {
  if ('0' <= c && c <= '9') { return c - '0'; }
  if ('a' <= c && c <= 'f') { return c + 10 - 'a'; }
  if ('A' <= c && c <= 'F') { return c + 10 - 'A'; }
  return -1;
}

void check_for_file(char drive_letter) {
  TCHAR szTempFile[MAX_PATH];

  char bytes[16];
  DWORD bytes_read;
  int i;

  StringCchPrintf(szTempFile, MAX_PATH, TEXT("%c:\\card0.txt"), drive_letter);
  HANDLE hFile = CreateFile(
    szTempFile,
    GENERIC_READ,
    FILE_SHARE_READ,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_READONLY,
    NULL);

  if (hFile == INVALID_HANDLE_VALUE) {
    printf("CreateFile: %ld\n", GetLastError());
    return;
  }

  if (!ReadFile(hFile, &bytes, sizeof(bytes), &bytes_read, NULL)) {
    printf("ReadFile: %ld\n", GetLastError());
    return;
  }

  if (bytes_read < 16) {
    printf("bytes_read: %ld < 16\n", bytes_read);
    return;
  }

  printf("bytes_read = %ld\n", bytes_read);
  for (i = 0; i < 8; i++) {
    printf("%c%c", bytes[i * 2], bytes[i * 2 + 1]);
    uint8_t n = hexval(bytes[i * 2]) * 16 + hexval(bytes[i * 2 + 1]);
    ID[0][i] = n;
  }
  printf("\n");
}

BOOL RegisterGuid(HWND hWnd, HDEVNOTIFY *hDeviceNotify) {
  DEV_BROADCAST_DEVICEINTERFACE notification_filter;

  memset(&notification_filter, 0, sizeof(DEV_BROADCAST_DEVICEINTERFACE));
  notification_filter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
  notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
  notification_filter.dbcc_classguid = target_guid;

  *hDeviceNotify = RegisterDeviceNotification(hWnd, &notification_filter, DEVICE_NOTIFY_WINDOW_HANDLE);

  if (*hDeviceNotify = NULL) {
    printf("RegisterDeviceNotification\n");
    return FALSE;
  }

  return TRUE;
}

INT_PTR WINAPI WinProcCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  LRESULT lRet = 1;
  static HDEVNOTIFY hDeviceNotify;

  switch (message) {
    case WM_CREATE:
      if (!RegisterGuid(hWnd, &hDeviceNotify)) {
        ExitProcess(1);
      }
      break;

    case WM_CLOSE:
      if (!UnregisterDeviceNotification(hDeviceNotify)) {
        printf("UnregisterDeviceNotification\n");
        ExitProcess(1);
      }
      break;

    case WM_DESTROY:
      PostQuitMessage(0);
      break;

    case WM_DEVICECHANGE:
    {
      switch (wParam) {
        case DBT_DEVICEARRIVAL:
          printf("Message: DBT_DEVICEARRIVAL\n");
          break;
        case DBT_DEVICEREMOVECOMPLETE:
          printf("Message: DBT_DEVICEREMOVECOMPLETE\n");
          break;
        case DBT_DEVNODES_CHANGED:
          // Don't care
          break;
        default:
          printf("Message: WM_DEVICECHANGE message received, value %I64u unhandled\n", wParam);
          break;
      }

      if (lParam && (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)) {
        PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR) lParam;

        switch (pHdr->dbch_devicetype) {
          case DBT_DEVTYP_DEVICEINTERFACE:
          {
            PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE) pHdr;
            printf(" -> DBT_DEVTYP_DEVICEINTERFACE => name: %s\n", pDevInf->dbcc_name);
            break;
          }
          case DBT_DEVTYP_HANDLE:
            printf(" -> DBT_DEVTYP_HANDLE\n");
            break;
          case DBT_DEVTYP_OEM:
            printf(" -> DBT_DEVTYP_OEM\n");
            break;
          case DBT_DEVTYP_PORT:
            printf(" -> DBT_DEVTYP_PORT\n");
            break;
          case DBT_DEVTYP_VOLUME:
          {
            PDEV_BROADCAST_VOLUME pDevVolume = (PDEV_BROADCAST_VOLUME) pHdr;
            char drive_letter = FirstDriveFromMask(pDevVolume->dbcv_unitmask);
            printf(" -> DBT_DEVTYP_VOLUME => Drive %c %s\n",
              drive_letter,
              (wParam == DBT_DEVICEARRIVAL) ? "has arrived" : "was removed");
            check_for_file(drive_letter);
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
  wnd_class.lpfnWndProc = WinProcCallback;
  wnd_class.cbClsExtra = 0;
  wnd_class.cbWndExtra = 0;
  wnd_class.hIcon = NULL;
  wnd_class.hbrBackground = NULL;
  wnd_class.hCursor = NULL;
  wnd_class.lpszClassName = WND_CLASS_NAME;
  wnd_class.lpszMenuName = NULL;
  wnd_class.hIconSm = NULL;

  if (!RegisterClassEx(&wnd_class)) {
    printf("RegisterClassEx\n");
    return FALSE;
  }

  return TRUE;
}

int wWinMain(HINSTANCE hInstanceExe, HINSTANCE hPrevInstance, LPWSTR lpstrCmdLine, int nCmdShow) {
  PWSTR g_pszAppName;
  MSG msg;
  int ret_val;

  int argc = 0;
  PWSTR *argv = CommandLineToArgvW(lpstrCmdLine, &argc);
  g_pszAppName = argv[0];

  if (!InitWindowClass()) {
    return -1;
  }

  HWND hWnd = CreateWindowEx(
      0,
      WND_CLASS_NAME,
      (LPCSTR) g_pszAppName,
      WS_DISABLED,
      0, 0,
      CW_USEDEFAULT, CW_USEDEFAULT,
      NULL,
      NULL,
      hInstanceExe,
      NULL);

  if (hWnd == NULL) {
    printf("CreateWindowEx\n");
    return -1;
  }

  while ((ret_val = GetMessage(&msg, NULL, 0, 0)) != 0) {
    if (ret_val == -1) {
      printf("GetMessage\n");
      return -1;
    } else {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return 0;
}
