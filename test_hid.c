#include <windows.h>
#include <stdio.h>

#include "hid.h"
#include "window.h"

int WINAPI wWinMain(HINSTANCE hInstanceExe, HINSTANCE hPrevInstance, LPWSTR lpstrCmdLine, int nCmdShow) {
  struct eamio_hid_device hid_ctx;
  HWND hWnd;

  hid_ctx_init(&hid_ctx);

  if (!InitWindowClass()) {
    return 1;
  }

  hWnd = CreateTheWindow(hInstanceExe);
  if (hWnd == NULL) {
    return 1;
  }

  hid_scan(&hid_ctx);
  printf("devices scanned\n");

  if (!hid_ctx.initialized) {
    printf("HID reader not found\n");
    return 1;
  }

  /*
  hid_poll_value_t poll_value;
  for (int i = 0; i < 5; i++) {
    while ((poll_value = hid_device_poll(&hid_ctx)) == HID_POLL_CARD_NOT_READY);
    if (poll_value == HID_POLL_ERROR) {
      printf("Error polling reader\n");
      return 1;
    }

    hid_device_read(&hid_ctx);
  }
  printf("program exit\n");
  */

  hid_free(&hid_ctx);

  if (!MessagePump(hWnd)) {
    return 1;
  }

  return 0;
}
