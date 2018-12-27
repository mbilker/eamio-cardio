/*
 * Copyright 2018 Matt Bilker <me@mbilker.us>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <windows.h>
#include <stdio.h>

#include "hid.h"
#include "window.h"

static BOOL hid_device_found() {
  size_t i;

  for (i = 0; i < CONTEXTS_LENGTH; i++) {
    if (CONTEXTS[i].initialized) {
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
    EnterCriticalSection(&HID_LOCK);

    hid_poll_value_t poll_value;
    int i = 0;
    while (i < 5) {
      for (int j = 0; j < CONTEXTS_LENGTH; j++) {
        if (CONTEXTS[j].initialized) {
          poll_value = hid_device_poll(&CONTEXTS[j]);

          if (poll_value == HID_POLL_ERROR) {
            printf("Error polling reader\n");
            return 1;
          }

          if (poll_value == HID_POLL_CARD_READY) {
            hid_device_read(&CONTEXTS[j]);
            i++;
          }
        }
      }
    }

    LeaveCriticalSection(&HID_LOCK);

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
