#include <windows.h>
#include <stdio.h>

#include "window.h"

int WINAPI wWinMain(HINSTANCE hInstanceExe, HINSTANCE hPrevInstance, LPWSTR lpstrCmdLine, int nCmdShow) {
  HWND hWnd;

  if (!InitWindowClass()) {
    return -1;
  }

  if ((hWnd = CreateTheWindow(hInstanceExe)) == NULL) {
    return -1;
  }

  if (!MessagePump(hWnd)) {
    return -1;
  }

  return 0;
}
