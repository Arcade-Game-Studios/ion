//
// Ion Engine
// macOS Window Implementation (Cocoa)
//
#include <ion/platform/Window.hpp>

#include <Cocoa/Cocoa.h>

namespace ion {

struct Window::Impl {
    NSApplication* app = nil;
    NSWindow* window = nil;
    NSMenu* mainMenu = nil;
    WindowConfig config;
    bool open = false;
};

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
        [window center];
        [window makeKeyAndOrderFront:nil];

        impl->window = window;
        impl->app = app;
        impl->open = true;

        [app finishLaunching];
        if (@available(macOS 14.0, *)) {
            [app activate];
        } else {
            [app activateIgnoringOtherApps:YES];
        }
    }

    return true;
}

void Window::destroy() {
    if (!impl) {
        return;
    }
    @autoreleasepool {
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

} // namespace ion
