//
// Ion Engine
// macOS Window Implementation (Cocoa)
//
#include <ion/platform/Window.hpp>
#include <ion/platform/Paths.hpp>

#include <Cocoa/Cocoa.h>

#include "DefaultIcon.hpp"

namespace ion {

struct Window::Impl {
    NSApplication* app = nil;
    NSWindow* window = nil;
    NSMenu* mainMenu = nil;
    id resizeObserver = nil;
    WindowConfig config;
    bool open = false;
};

namespace {

constexpr CGFloat DOCK_ICON_MARGIN_RATIO = 0.10f;

NSString* findPlatformIconPath() {
    NSString* dir =
        [NSString stringWithUTF8String:ion::getAssetPath("assets/icons/macos").c_str()];
    NSArray* files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir
                                                                        error:nil];
    for (NSString* file in files) {
        if ([file hasSuffix:@".png"] || [file hasSuffix:@".icns"]) {
            return [dir stringByAppendingPathComponent:file];
        }
    }
    return nil;
}

NSImage* makeDockIcon(NSImage* source) {
    NSSize size = [source size];
    if (size.width < 32.0 || size.height < 32.0) {
        return source;
    }

    CGFloat margin = size.width * DOCK_ICON_MARGIN_RATIO;
    NSImage* icon = [[NSImage alloc] initWithSize:size];
    [icon lockFocus];
    [source drawInRect:NSMakeRect(margin, margin,
                                  size.width - margin * 2.0,
                                  size.height - margin * 2.0)
              fromRect:NSZeroRect
             operation:NSCompositingOperationSourceOver
              fraction:1.0];
    [icon unlockFocus];
    return icon;
}

} // namespace

Window::Window() : impl(new Impl) {
}

Window::Window(const WindowConfig& config) : impl(new Impl) {
    impl->config = config;
}

Window::Window(Window&& other) noexcept : impl(other.impl) {
    other.impl = nullptr;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        destroy();
        delete impl;
        impl = other.impl;
        other.impl = nullptr;
    }
    return *this;
}

Window::~Window() {
    destroy();
    delete impl;
}

bool Window::create() {
    if (!impl) {
        impl = new Impl;
    }
    if (impl->window) {
        return true;
    }

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSString* processName =
            [NSString stringWithUTF8String:impl->config.appName.c_str()];
        if (processName.length > 0) {
            [[NSProcessInfo processInfo] setProcessName:processName];
        }

        NSString* appName = [[NSProcessInfo processInfo] processName];
        NSMenu* mainMenu = [[NSMenu alloc] init];
        NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
        [mainMenu addItem:appMenuItem];
        NSMenu* appMenu = [[NSMenu alloc] init];
        [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:appName]
                           action:@selector(terminate:)
                    keyEquivalent:@"q"];
        [appMenuItem setSubmenu:appMenu];
        [app setMainMenu:mainMenu];
        impl->mainMenu = mainMenu;

        NSRect contentRect = NSMakeRect(0, 0,
                                        (CGFloat)impl->config.width,
                                        (CGFloat)impl->config.height);
        NSUInteger styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                            | NSWindowStyleMaskMiniaturizable;
        if (impl->config.resizable) {
            styleMask |= NSWindowStyleMaskResizable;
        }

        NSWindow* window = [[NSWindow alloc] initWithContentRect:contentRect
                                                       styleMask:styleMask
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        [window setTitle:[NSString stringWithUTF8String:impl->config.title.c_str()]];
        [window setReleasedWhenClosed:NO];

        NSString* iconPath = nil;
        if (!impl->config.iconPath.empty()) {
            iconPath =
                [NSString stringWithUTF8String:impl->config.iconPath.c_str()];
        } else {
            iconPath = findPlatformIconPath();
        }

        if (iconPath) {
            NSImage* icon = [[NSImage alloc] initWithContentsOfFile:iconPath];
            if (icon) {
                [app setApplicationIconImage:makeDockIcon(icon)];
                [window setRepresentedURL:[NSURL fileURLWithPath:iconPath]];
            }
        } else {
            NSData* data =
                [NSData dataWithBytes:DEFAULT_WINDOW_ICON_DATA
                               length:DEFAULT_WINDOW_ICON_SIZE];
            NSImage* icon = [[NSImage alloc] initWithData:data];
            if (icon) {
                [app setApplicationIconImage:makeDockIcon(icon)];
            }
        }

        [window center];
        [window makeKeyAndOrderFront:nil];

        __block Window::Impl* blockImpl = impl;
        impl->resizeObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSWindowDidResizeNotification
                        object:window
                         queue:nil
                    usingBlock:^(NSNotification* note) {
                        NSWindow* w = (NSWindow*)note.object;
                        NSRect content = [w contentRectForFrameRect:[w frame]];
                        blockImpl->config.width = (uint32_t)content.size.width;
                        blockImpl->config.height = (uint32_t)content.size.height;
                        if (blockImpl->config.onResize) {
                            blockImpl->config.onResize((uint32_t)content.size.width,
                                                       (uint32_t)content.size.height);
                        }
                    }];

        impl->window = window;
        impl->app = app;
        impl->open = true;

        [app finishLaunching];
        if (@available(macOS 14.0, *)) {
            [app activate];
        } else {
            [app activateIgnoringOtherApps:YES];
        }

        if (impl->config.fullscreen) {
            setFullscreen(true);
        }
    }

    return true;
}

void Window::destroy() {
    if (!impl) {
        return;
    }
    @autoreleasepool {
        if (impl->resizeObserver) {
            [[NSNotificationCenter defaultCenter] removeObserver:impl->resizeObserver];
            impl->resizeObserver = nil;
        }
        if (impl->window) {
            [impl->window close];
            impl->window = nil;
        }
        impl->open = false;
    }
}

bool Window::isOpen() const {
    return impl && impl->open && impl->window && [impl->window isVisible];
}

void Window::close() {
    impl->open = false;
    if (!impl->window) {
        return;
    }
    @autoreleasepool {
        [impl->window close];
    }
}

void Window::pollEvents() {
    if (!impl) {
        return;
    }
    @autoreleasepool {
        while (impl->app) {
            NSEvent* event = [impl->app nextEventMatchingMask:NSEventMaskAny
                                                    untilDate:[NSDate distantPast]
                                                       inMode:NSDefaultRunLoopMode
                                                      dequeue:YES];
            if (!event) {
                break;
            }
            [impl->app sendEvent:event];
        }
    }
}

uint32_t Window::width() const {
    @autoreleasepool {
        return (uint32_t)[impl->window contentRectForFrameRect:[impl->window frame]].size.width;
    }
}

uint32_t Window::height() const {
    @autoreleasepool {
        return (uint32_t)[impl->window contentRectForFrameRect:[impl->window frame]].size.height;
    }
}

void* Window::nativeHandle() {
    return (void*)CFBridgingRetain(impl->window);
}

void Window::setFullscreen(bool fullscreen) {
    if (!impl || !impl->window) {
        return;
    }
    @autoreleasepool {
        if (impl->config.fullscreen == fullscreen) {
            return;
        }
        impl->config.fullscreen = fullscreen;
        [impl->window toggleFullScreen:nil];
    }
}

bool Window::isFullscreen() const {
    if (!impl || !impl->window) {
        return impl ? impl->config.fullscreen : false;
    }
    return ([impl->window styleMask] & NSWindowStyleMaskFullScreen) != 0;
}

} // namespace ion
