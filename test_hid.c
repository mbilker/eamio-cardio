#include <windows.h>
#include <stdio.h>

#include "hid.h"
#include "window.h"

static BOOL hid_device_found() {
  size_t i;

  for (i = 0; i < contexts_length; i++) {
    if (contexts[i].initialized) {
      return TRUE;
    }
  }

  return FALSE;
}

int WINAPI wWinMain(HINSTANCE hInstanceExe, HINSTANCE hPrevInstance, LPWSTR lpstrCmdLine, int nCmdShow) {
  HWND hWnd;

  hid_init();

  if (!InitWindowClass()) {
    return 1;
  }

  if (hid_scan()) {
    printf("device scan successful\n");
  } else {
    printf("device scan error\n");
  }

  hWnd = CreateTheWindow(hInstanceExe);
  if (hWnd == NULL) {
    return 1;
  }

  if (hid_device_found()) {
    EnterCriticalSection(&crit_section);

    hid_poll_value_t poll_value;
    int i = 0;
    while (i < 5) {
      for (int j = 0; j < contexts_length; j++) {
        if (contexts[j].initialized) {
          poll_value = hid_device_poll(&contexts[j]);

          if (poll_value == HID_POLL_ERROR) {
            printf("Error polling reader\n");
            return 1;
          }

          if (poll_value == HID_POLL_CARD_READY) {
            hid_device_read(&contexts[j]);
            i++;
          }
        }
      }
    }

    LeaveCriticalSection(&crit_section);

    printf("program exit\n");
  } else {
    printf("HID reader not found\n");
  }

  if (!MessagePump(hWnd)) {
    return 1;
  }

  hid_close();

  return 0;
}
