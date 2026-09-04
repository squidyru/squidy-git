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
    if (host == nullptr || host->property("squidyVibrancyInstalled").toBool()) {
        return;
    }
    NSView *hostView = nativeView(host);
    NSView *containerView = hostView.superview;
    if (hostView == nil || containerView == nil) {
        return;
    }

    // Cover the complete native container, not the central widget's client
    // rectangle. Its title-bar offset can leave an unblurred band at the bottom.
    auto *effect = [[SquidyVisualEffectView alloc] initWithFrame:containerView.bounds];
    effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    effect.material = NSVisualEffectMaterialSidebar;
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.state = NSVisualEffectStateActive;
    effect.emphasized = NO;
    effect.appearance = [NSAppearance appearanceNamed:NSAppearanceNameVibrantDark];
    [containerView addSubview:effect positioned:NSWindowBelow relativeTo:hostView];
    host->setProperty("squidyVibrancyInstalled", true);
}

void configureWindow(QWidget *window) {
    NSView *contentView = nativeView(window);
    NSWindow *nativeWindow = contentView.window;
    if (nativeWindow == nil) {
        return;
    }
    nativeWindow.opaque = NO;
    nativeWindow.backgroundColor = NSColor.clearColor;
    nativeWindow.titlebarAppearsTransparent = YES;
    nativeWindow.hasShadow = YES;
    [nativeWindow invalidateShadow];

    if (!window->property("squidyTrafficLightsAdjusted").toBool()) {
        window->setProperty("squidyTrafficLightsAdjusted", true);
        dispatch_async(dispatch_get_main_queue(), ^{
            for (const NSWindowButton type : {NSWindowCloseButton,
                                              NSWindowMiniaturizeButton,
                                              NSWindowZoomButton}) {
                NSButton *button = [nativeWindow standardWindowButton:type];
                if (button == nil) {
                    continue;
                }
                NSPoint origin = button.frame.origin;
                origin.x += 4.0;
                origin.y -= 16.0;
                [button setFrameOrigin:origin];
            }
        });
    }
}

}
