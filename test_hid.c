#include <windows.h>
#include <stdio.h>

#include "hid.h"

int WINAPI wWinMain(HINSTANCE hInstanceExe, HINSTANCE hPrevInstance, LPWSTR lpstrCmdLine, int nCmdShow) {
  struct eamio_hid_device hid_ctx;
  hid_ctx_init(&hid_ctx);

  hid_scan(&hid_ctx);
  printf("devices scanned\n");

  for (int i = 0; i < 5; i++) {
    while (!hid_device_poll(&hid_ctx));
    hid_device_read(&hid_ctx);
  }
  printf("program exit\n");

  hid_free(&hid_ctx);
  return 0;
}
