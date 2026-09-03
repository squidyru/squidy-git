// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "windowsbackdrop.h"

#include <QWidget>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dwmapi.h>

namespace {

// Numeric values keep MinGW builds compatible with Windows SDK headers that
// predate the Windows 11 backdrop declarations.
constexpr int ImmersiveDarkModeAttribute = 20;
constexpr int WindowCornerPreferenceAttribute = 33;
constexpr int SystemBackdropTypeAttribute = 38;
constexpr int RoundedCorners = 2;
constexpr int TabbedWindowBackdrop = 4;

}

namespace WindowsBackdrop {

bool apply(QWidget *window, const bool dark) {
    if (window == nullptr) {
        return false;
    }

    const HWND handle = reinterpret_cast<HWND>(window->winId());
    if (handle == nullptr) {
        return false;
    }

    const BOOL darkMode = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(handle,
                          static_cast<DWMWINDOWATTRIBUTE>(ImmersiveDarkModeAttribute),
                          &darkMode, sizeof(darkMode));

    const int cornerPreference = RoundedCorners;
    DwmSetWindowAttribute(handle,
                          static_cast<DWMWINDOWATTRIBUTE>(WindowCornerPreferenceAttribute),
                          &cornerPreference, sizeof(cornerPreference));

    const int backdrop = TabbedWindowBackdrop;
    const HRESULT result = DwmSetWindowAttribute(
        handle, static_cast<DWMWINDOWATTRIBUTE>(SystemBackdropTypeAttribute),
        &backdrop, sizeof(backdrop));
    return SUCCEEDED(result);
}

}
