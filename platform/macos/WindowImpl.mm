//
// Ion Engine
// macOS Window Implementation (Cocoa)
//
#include <ion/platform/Window.hpp>
#include <ion/platform/Paths.hpp>
#include <ion/platform/Input.hpp>

#include <Cocoa/Cocoa.h>

#include "DefaultIcon.hpp"

// Content view that swallows key events instead of letting the responder
// chain NSBeep on unhandled keys. Ion reads raw key events directly from the
// event queue in pollEvents(), so nothing here needs to reach a responder.
// Command-modified equivalents (e.g. Cmd+Q) still pass through to the menu.
@interface IonGameView : NSView
@end

@implementation IonGameView

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)keyDown:(NSEvent*)event {
    (void)event;
}

- (void)keyUp:(NSEvent*)event {
    (void)event;
}

- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        return [super performKeyEquivalent:event];
    }
    return YES;
}

@end

namespace ion {

struct Window::Impl {
    NSApplication* app = nil;
    NSWindow* window = nil;
    NSMenu* mainMenu = nil;
    id resizeObserver = nil;
    NSEventModifierFlags lastModifiers = 0;
    WindowConfig config;
    bool open = false;
};

namespace {

constexpr CGFloat DOCK_ICON_MARGIN_RATIO = 0.10f;

Key keyFromVirtualCode(unsigned short code) {
    switch (code) {
    case 0x00: return Key::A;
    case 0x01: return Key::S;
    case 0x02: return Key::D;
    case 0x03: return Key::F;
    case 0x04: return Key::H;
    case 0x05: return Key::G;
    case 0x06: return Key::Z;
    case 0x07: return Key::X;
    case 0x08: return Key::C;
    case 0x09: return Key::V;
    case 0x0B: return Key::B;
    case 0x0C: return Key::Q;
    case 0x0D: return Key::W;
    case 0x0E: return Key::E;
    case 0x0F: return Key::R;
    case 0x10: return Key::Y;
    case 0x11: return Key::T;
    case 0x12: return Key::Num1;
    case 0x13: return Key::Num2;
    case 0x14: return Key::Num3;
    case 0x15: return Key::Num4;
    case 0x16: return Key::Num6;
    case 0x17: return Key::Num5;
    case 0x18: return Key::Equal;
    case 0x19: return Key::Num9;
    case 0x1A: return Key::Num7;
    case 0x1B: return Key::Minus;
    case 0x1C: return Key::Num8;
    case 0x1D: return Key::Num0;
    case 0x1E: return Key::RightBracket;
    case 0x1F: return Key::O;
    case 0x20: return Key::U;
    case 0x21: return Key::LeftBracket;
    case 0x22: return Key::I;
    case 0x23: return Key::P;
    case 0x24: return Key::Enter;
    case 0x25: return Key::L;
    case 0x26: return Key::J;
    case 0x27: return Key::Apostrophe;
    case 0x28: return Key::K;
    case 0x29: return Key::Semicolon;
    case 0x2A: return Key::Backslash;
    case 0x2B: return Key::Comma;
    case 0x2C: return Key::Slash;
    case 0x2D: return Key::N;
    case 0x2E: return Key::M;
    case 0x2F: return Key::Period;
    case 0x30: return Key::Tab;
    case 0x31: return Key::Space;
    case 0x32: return Key::Backtick;
    case 0x33: return Key::Backspace;
    case 0x35: return Key::Escape;
    case 0x37: return Key::LeftSuper;
    case 0x38: return Key::LeftShift;
    case 0x39: return Key::CapsLock;
    case 0x3A: return Key::LeftAlt;
    case 0x3B: return Key::LeftControl;
    case 0x3C: return Key::RightShift;
    case 0x3D: return Key::RightAlt;
    case 0x3E: return Key::RightControl;
    case 0x40: return Key::F17;
    case 0x41: return Key::KeypadDecimal;
    case 0x43: return Key::KeypadMultiply;
    case 0x45: return Key::KeypadAdd;
    case 0x47: return Key::Unknown;
    case 0x4B: return Key::KeypadDivide;
    case 0x4C: return Key::KeypadEnter;
    case 0x4E: return Key::KeypadSubtract;
    case 0x4F: return Key::F18;
    case 0x50: return Key::F19;
    case 0x51: return Key::KeypadEqual;
    case 0x52: return Key::Keypad0;
    case 0x53: return Key::Keypad1;
    case 0x54: return Key::Keypad2;
    case 0x55: return Key::Keypad3;
    case 0x56: return Key::Keypad4;
    case 0x57: return Key::Keypad5;
    case 0x58: return Key::Keypad6;
    case 0x59: return Key::Keypad7;
    case 0x5A: return Key::F20;
    case 0x5B: return Key::Keypad8;
    case 0x5C: return Key::Keypad9;
    case 0x60: return Key::F5;
    case 0x61: return Key::F6;
    case 0x62: return Key::F7;
    case 0x63: return Key::F3;
    case 0x64: return Key::F8;
    case 0x65: return Key::F9;
    case 0x67: return Key::F11;
    case 0x69: return Key::F13;
    case 0x6A: return Key::F16;
    case 0x6B: return Key::F14;
    case 0x6D: return Key::F10;
    case 0x6F: return Key::F12;
    case 0x71: return Key::F15;
    case 0x72: return Key::F3;
    case 0x73: return Key::Home;
    case 0x74: return Key::PageUp;
    case 0x75: return Key::Delete;
    case 0x76: return Key::F4;
    case 0x77: return Key::End;
    case 0x78: return Key::F2;
    case 0x79: return Key::PageDown;
    case 0x7A: return Key::F1;
    case 0x7B: return Key::ArrowLeft;
    case 0x7C: return Key::ArrowRight;
    case 0x7D: return Key::ArrowDown;
    case 0x7E: return Key::ArrowUp;
    default: return Key::Unknown;
    }
}

MouseButton mouseButtonFromEvent(NSEvent* event) {
    switch (event.buttonNumber) {
    case 0: return MouseButton::Left;
    case 1: return MouseButton::Right;
    case 2: return MouseButton::Middle;
    case 3: return MouseButton::Button4;
    case 4: return MouseButton::Button5;
    default: return MouseButton::None;
    }
}

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
        [window setAcceptsMouseMovedEvents:YES];
        [window setContentView:[[IonGameView alloc] initWithFrame:contentRect]];

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

            switch (event.type) {
            case NSEventTypeKeyDown:
                input::platformKeyDown(keyFromVirtualCode(event.keyCode));
                break;
            case NSEventTypeKeyUp:
                input::platformKeyUp(keyFromVirtualCode(event.keyCode));
                break;
            case NSEventTypeFlagsChanged: {
                NSEventModifierFlags flags = event.modifierFlags;
                NSEventModifierFlags old = impl->lastModifiers;
                if ((flags & NSEventModifierFlagShift) && !(old & NSEventModifierFlagShift)) {
                    input::platformKeyDown(Key::LeftShift);
                } else if (!(flags & NSEventModifierFlagShift) &&
                           (old & NSEventModifierFlagShift)) {
                    input::platformKeyUp(Key::LeftShift);
                }
                if ((flags & NSEventModifierFlagControl) &&
                    !(old & NSEventModifierFlagControl)) {
                    input::platformKeyDown(Key::LeftControl);
                } else if (!(flags & NSEventModifierFlagControl) &&
                           (old & NSEventModifierFlagControl)) {
                    input::platformKeyUp(Key::LeftControl);
                }
                if ((flags & NSEventModifierFlagOption) &&
                    !(old & NSEventModifierFlagOption)) {
                    input::platformKeyDown(Key::LeftAlt);
                } else if (!(flags & NSEventModifierFlagOption) &&
                           (old & NSEventModifierFlagOption)) {
                    input::platformKeyUp(Key::LeftAlt);
                }
                if ((flags & NSEventModifierFlagCommand) &&
                    !(old & NSEventModifierFlagCommand)) {
                    input::platformKeyDown(Key::LeftSuper);
                } else if (!(flags & NSEventModifierFlagCommand) &&
                           (old & NSEventModifierFlagCommand)) {
                    input::platformKeyUp(Key::LeftSuper);
                }
                impl->lastModifiers = flags;
                break;
            }
            case NSEventTypeLeftMouseDown:
                input::platformMouseDown(MouseButton::Left);
                break;
            case NSEventTypeLeftMouseUp:
                input::platformMouseUp(MouseButton::Left);
                break;
            case NSEventTypeRightMouseDown:
                input::platformMouseDown(MouseButton::Right);
                break;
            case NSEventTypeRightMouseUp:
                input::platformMouseUp(MouseButton::Right);
                break;
            case NSEventTypeOtherMouseDown:
                input::platformMouseDown(mouseButtonFromEvent(event));
                break;
            case NSEventTypeOtherMouseUp:
                input::platformMouseUp(mouseButtonFromEvent(event));
                break;
            case NSEventTypeMouseMoved:
            case NSEventTypeLeftMouseDragged:
            case NSEventTypeRightMouseDragged:
            case NSEventTypeOtherMouseDragged: {
                NSPoint point = [event locationInWindow];
                NSSize size =
                    [impl->window contentRectForFrameRect:[impl->window frame]].size;
                input::platformMouseMove((float)point.x,
                                         (float)(size.height - point.y));
                break;
            }
            case NSEventTypeScrollWheel:
                input::platformMouseScroll((float)event.scrollingDeltaX,
                                           (float)event.scrollingDeltaY);
                break;
            default:
                break;
            }
        }
    }
    ion::input::pollGamepads();
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
    return (__bridge void*)[impl->window contentView];
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
