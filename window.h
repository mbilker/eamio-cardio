#include <windows.h>
#include <stdint.h>

#define WND_CLASS_NAME TEXT("cardio Notification Window")

typedef void (*log_func_t)(const char *fmt, ...)
  __attribute__(( format(printf, 1, 2) ));

extern log_func_t log_f;

void set_log_func(log_func_t log_func);
BOOL InitWindowClass();
HWND CreateTheWindow(HINSTANCE hInstance);
BOOL MessagePump(HWND hWnd);
BOOL EndTheWindow(HWND hWnd);
