#include <windows.h>
#include <stdio.h>

#include "hid.h"

int WINAPI wWinMain(HINSTANCE hInstanceExe, HINSTANCE hPrevInstance, LPWSTR lpstrCmdLine, int nCmdShow) {
  scan_for_device();

  return 0;
}
