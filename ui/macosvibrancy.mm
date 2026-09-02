// Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

#include "macosvibrancy.h"

#include <QWidget>

#import <AppKit/AppKit.h>

@interface SquidyVisualEffectView : NSVisualEffectView
@end

@implementation SquidyVisualEffectView

- (NSView *)hitTest:(NSPoint)point {
    Q_UNUSED(point);
    return nil;
}

@end

namespace {

NSView *nativeView(QWidget *widget) {
    if (widget == nullptr) {
        return nil;
    }
    widget->setAttribute(Qt::WA_NativeWindow, true);
    return (__bridge NSView *)(reinterpret_cast<void *>(widget->winId()));
}

}

namespace MacOSVibrancy {

void install(QWidget *host) {
    NSView *hostView = nativeView(host);
    NSView *containerView = hostView.superview;
    if (hostView == nil || containerView == nil) {
        return;
    }

    auto *effect = [[SquidyVisualEffectView alloc] initWithFrame:hostView.frame];
    effect.autoresizingMask = hostView.autoresizingMask;
    effect.material = NSVisualEffectMaterialUnderWindowBackground;
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.state = NSVisualEffectStateActive;
    effect.emphasized = NO;
    [containerView addSubview:effect positioned:NSWindowBelow relativeTo:hostView];
}

void configureWindow(QWidget *window) {
    NSView *contentView = nativeView(window);
    NSWindow *nativeWindow = contentView.window;
    if (nativeWindow == nil) {
        return;
    }
    nativeWindow.opaque = NO;
    nativeWindow.backgroundColor = [NSColor colorWithSRGBRed:11.0 / 255.0
                                                       green:52.0 / 255.0
                                                        blue:78.0 / 255.0
                                                       alpha:1.0];
    nativeWindow.titlebarAppearsTransparent = YES;
}

}
