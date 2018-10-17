#ifndef _WINDOW_H
#define _WINDOW_H

#include <windows.h>
#include <stdint.h>

#define WND_CLASS_NAME TEXT("cardio Notification Window")

BOOL InitWindowClass();
HWND CreateTheWindow(HINSTANCE hInstance);
BOOL MessagePump(HWND hWnd);
BOOL EndTheWindow(HWND hWnd);

#endif
