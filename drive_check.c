#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <strsafe.h>

#include "window.h"

uint8_t ID[2][8];
uint8_t ID_TIMER[2];

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
    log_f("CreateFile error: %ld", GetLastError());
    return;
  }

  if (!ReadFile(hFile, &bytes, sizeof(bytes), &bytes_read, NULL)) {
    log_f("ReadFile error: %ld", GetLastError());
    return;
  }

  if (bytes_read < 16) {
    log_f("bytes_read: %ld < 16", bytes_read);
    return;
  }

  for (i = 0; i < 8; i++) {
    uint8_t n = hexval(bytes[i * 2]) * 16 + hexval(bytes[i * 2 + 1]);
    ID[0][i] = n;
  }
  ID_TIMER[0] = 32;

  log_f("ID: %02X%02X%02X%02X%02X%02X%02X%02X",
    ID[0][0],
    ID[0][1],
    ID[0][2],
    ID[0][3],
    ID[0][4],
    ID[0][5],
    ID[0][6],
    ID[0][7]);
}
