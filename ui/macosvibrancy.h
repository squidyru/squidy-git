// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#pragma once

class QWidget;

namespace MacOSVibrancy {

/// Places one full-window AppKit visual-effect view behind the Qt backing store.
void install(QWidget *host);

/// Keeps the native NSWindow transparent so the visual effect can sample the desktop.
void configureWindow(QWidget *window);

}
