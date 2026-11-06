// Funkcje user32, które w nagłówkach MinGW (gcc 16, clang64/ucrt64) nie są widoczne po <windows.h>
// (GetWindowRect, GetClientRect, GetCursorPos, ScreenToClient, GetAsyncKeyState).
// Wołane przez GetProcAddress, więc nie zależą od układu nagłówków.
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace win32_compat {

template <class Fn>
Fn user32_function(const char* name) {
    HMODULE module = GetModuleHandleW(L"user32.dll");
    if (!module) module = LoadLibraryW(L"user32.dll");
    return module ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, name))) : nullptr;
}

inline BOOL GetWindowRect(HWND window, RECT* rect) {
    static auto function = user32_function<BOOL(WINAPI*)(HWND, RECT*)>("GetWindowRect");
    return function ? function(window, rect) : FALSE;
}
inline BOOL GetClientRect(HWND window, RECT* rect) {
    static auto function = user32_function<BOOL(WINAPI*)(HWND, RECT*)>("GetClientRect");
    return function ? function(window, rect) : FALSE;
}
inline BOOL GetCursorPos(POINT* point) {
    static auto function = user32_function<BOOL(WINAPI*)(POINT*)>("GetCursorPos");
    return function ? function(point) : FALSE;
}
inline BOOL ScreenToClient(HWND window, POINT* point) {
    static auto function = user32_function<BOOL(WINAPI*)(HWND, POINT*)>("ScreenToClient");
    return function ? function(window, point) : FALSE;
}
inline SHORT GetAsyncKeyState(int key) {
    static auto function = user32_function<SHORT(WINAPI*)(int)>("GetAsyncKeyState");
    return function ? function(key) : 0;
}

}  // namespace win32_compat
#endif
