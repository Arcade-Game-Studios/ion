#include <ion/platform/Window.hpp>
#include <ion/platform/Input.hpp>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cstring>
#include <mutex>

namespace ion {

namespace {

Key keyFromKeysym(KeySym keysym) {
    if (keysym >= XK_A && keysym <= XK_Z) {
        return static_cast<Key>(static_cast<int>(Key::A) + (keysym - XK_A));
    }
    if (keysym >= XK_a && keysym <= XK_z) {
        return static_cast<Key>(static_cast<int>(Key::A) + (keysym - XK_a));
    }
    if (keysym >= XK_0 && keysym <= XK_9) {
        return static_cast<Key>(static_cast<int>(Key::Num0) + (keysym - XK_0));
    }
    if (keysym >= XK_F1 && keysym <= XK_F24) {
        return static_cast<Key>(static_cast<int>(Key::F1) + (keysym - XK_F1));
    }
    switch (keysym) {
    case XK_Escape: return Key::Escape;
    case XK_Tab:
    case XK_ISO_Left_Tab: return Key::Tab;
    case XK_Caps_Lock: return Key::CapsLock;
    case XK_space: return Key::Space;
    case XK_Return: return Key::Enter;
    case XK_BackSpace: return Key::Backspace;
    case XK_Delete: return Key::Delete;
    case XK_Insert: return Key::Insert;
    case XK_Home: return Key::Home;
    case XK_End: return Key::End;
    case XK_Page_Up: return Key::PageUp;
    case XK_Page_Down: return Key::PageDown;
    case XK_Left: return Key::ArrowLeft;
    case XK_Right: return Key::ArrowRight;
    case XK_Up: return Key::ArrowUp;
    case XK_Down: return Key::ArrowDown;
    case XK_Shift_L: return Key::LeftShift;
    case XK_Shift_R: return Key::RightShift;
    case XK_Control_L: return Key::LeftControl;
    case XK_Control_R: return Key::RightControl;
    case XK_Alt_L: return Key::LeftAlt;
    case XK_Alt_R: return Key::RightAlt;
    case XK_Super_L:
    case XK_Meta_L: return Key::LeftSuper;
    case XK_Super_R:
    case XK_Meta_R: return Key::RightSuper;
    case XK_Menu: return Key::Menu;
    case XK_semicolon: return Key::Semicolon;
    case XK_apostrophe: return Key::Apostrophe;
    case XK_comma: return Key::Comma;
    case XK_period: return Key::Period;
    case XK_slash: return Key::Slash;
    case XK_backslash: return Key::Backslash;
    case XK_minus: return Key::Minus;
    case XK_equal: return Key::Equal;
    case XK_bracketleft: return Key::LeftBracket;
    case XK_bracketright: return Key::RightBracket;
    case XK_grave: return Key::Backtick;
    case XK_KP_0: return Key::Keypad0;
    case XK_KP_1: return Key::Keypad1;
    case XK_KP_2: return Key::Keypad2;
    case XK_KP_3: return Key::Keypad3;
    case XK_KP_4: return Key::Keypad4;
    case XK_KP_5: return Key::Keypad5;
    case XK_KP_6: return Key::Keypad6;
    case XK_KP_7: return Key::Keypad7;
    case XK_KP_8: return Key::Keypad8;
    case XK_KP_9: return Key::Keypad9;
    case XK_KP_Decimal: return Key::KeypadDecimal;
    case XK_KP_Divide: return Key::KeypadDivide;
    case XK_KP_Multiply: return Key::KeypadMultiply;
    case XK_KP_Subtract: return Key::KeypadSubtract;
    case XK_KP_Add: return Key::KeypadAdd;
    case XK_KP_Enter: return Key::KeypadEnter;
    case XK_KP_Equal: return Key::KeypadEqual;
    default: return Key::Unknown;
    }
}

std::mutex g_xInitMutex;
int g_xInitRefs = 0;

Display* xOpenDisplay() {
    std::lock_guard<std::mutex> lock(g_xInitMutex);
    if (g_xInitRefs == 0) {
        if (!XInitThreads()) {
            return nullptr;
        }
    }
    Display* display = XOpenDisplay(nullptr);
    if (display) {
        ++g_xInitRefs;
    }
    return display;
}

void xCloseDisplay(Display* display) {
    std::lock_guard<std::mutex> lock(g_xInitMutex);
    if (display && g_xInitRefs > 0) {
        --g_xInitRefs;
        XCloseDisplay(display);
    }
}

} // namespace

struct Window::Impl {
    Display* display = nullptr;
    ::Window window = 0;
    Atom wmDelete = 0;
    int screen = 0;
    bool open = false;
    bool fullscreen = false;
    WindowConfig config;
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
    Display* display = xOpenDisplay();
    if (!display) {
        return false;
    }

    int screen = DefaultScreen(display);
    ::Window root = RootWindow(display, screen);

    XSetWindowAttributes attrs = {};
    attrs.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask |
                       KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask;

    unsigned long valueMask = CWEventMask;
    int width = (int)impl->config.width;
    int height = (int)impl->config.height;

    ::Window xw = XCreateWindow(display, root, 0, 0, width, height, 0,
                                CopyFromParent, InputOutput, CopyFromParent,
                                valueMask, &attrs);
    if (!xw) {
        xCloseDisplay(display);
        return false;
    }

    XStoreName(display, xw, impl->config.title.c_str());

    Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, xw, &wmDelete, 1);

    if (impl->config.resizable) {
        XSizeHints* sizeHints = XAllocSizeHints();
        if (sizeHints) {
            sizeHints->flags = PMinSize;
            sizeHints->min_width = width;
            sizeHints->min_height = height;
            XSetWMNormalHints(display, xw, sizeHints);
            XFree(sizeHints);
        }
    }

    XMapWindow(display, xw);
    XFlush(display);

    impl->display = display;
    impl->window = xw;
    impl->wmDelete = wmDelete;
    impl->screen = screen;
    impl->open = true;

    if (impl->config.fullscreen) {
        setFullscreen(true);
    }

    return true;
}

void Window::destroy() {
    if (!impl) {
        return;
    }
    if (impl->display && impl->window) {
        XDestroyWindow(impl->display, impl->window);
        impl->window = 0;
    }
    if (impl->display) {
        xCloseDisplay(impl->display);
        impl->display = nullptr;
    }
    impl->open = false;
}

bool Window::isOpen() const {
    return impl && impl->open;
}

void Window::close() {
    if (impl && impl->display && impl->window) {
        XEvent event = {};
        event.xclient.type = ClientMessage;
        event.xclient.window = impl->window;
        event.xclient.message_type = XInternAtom(impl->display, "WM_PROTOCOLS", False);
        event.xclient.format = 32;
        event.xclient.data.l[0] = (long)impl->wmDelete;
        event.xclient.data.l[1] = CurrentTime;
        XSendEvent(impl->display, impl->window, False, NoEventMask, &event);
        XFlush(impl->display);
    }
}

void Window::pollEvents() {
    if (!impl || !impl->display) {
        return;
    }
    while (XPending(impl->display) > 0) {
        XEvent event;
        XNextEvent(impl->display, &event);
        switch (event.type) {
        case ConfigureNotify:
            impl->config.width = (uint32_t)event.xconfigure.width;
            impl->config.height = (uint32_t)event.xconfigure.height;
            if (impl->config.onResize) {
                impl->config.onResize(impl->config.width, impl->config.height);
            }
            break;
        case KeyPress: {
            Key key = keyFromKeysym(XLookupKeysym(&event.xkey, 0));
            if (key != Key::Unknown) {
                input::platformKeyDown(key);
            }
            break;
        }
        case KeyRelease: {
            if (XPending(impl->display)) {
                XEvent next;
                XPeekEvent(impl->display, &next);
                if (next.type == KeyPress && next.xkey.keycode == event.xkey.keycode &&
                    next.xkey.time == event.xkey.time) {
                    break;
                }
            }
            Key key = keyFromKeysym(XLookupKeysym(&event.xkey, 0));
            if (key != Key::Unknown) {
                input::platformKeyUp(key);
            }
            break;
        }
        case ButtonPress:
        case ButtonRelease: {
            bool down = (event.type == ButtonPress);
            switch (event.xbutton.button) {
            case Button1:
                if (down) input::platformMouseDown(MouseButton::Left);
                else input::platformMouseUp(MouseButton::Left);
                break;
            case Button2:
                if (down) input::platformMouseDown(MouseButton::Middle);
                else input::platformMouseUp(MouseButton::Middle);
                break;
            case Button3:
                if (down) input::platformMouseDown(MouseButton::Right);
                else input::platformMouseUp(MouseButton::Right);
                break;
            case Button4:
                if (down) input::platformMouseScroll(0.0f, 1.0f);
                break;
            case Button5:
                if (down) input::platformMouseScroll(0.0f, -1.0f);
                break;
            case 6:
                if (down) input::platformMouseScroll(-1.0f, 0.0f);
                break;
            case 7:
                if (down) input::platformMouseScroll(1.0f, 0.0f);
                break;
            default:
                break;
            }
            break;
        }
        case MotionNotify:
            input::platformMouseMove((float)event.xmotion.x, (float)event.xmotion.y);
            break;
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == impl->wmDelete) {
                impl->open = false;
            }
            break;
        case DestroyNotify:
            impl->open = false;
            break;
        default:
            break;
        }
    }
    ion::input::pollGamepads();
}

uint32_t Window::width() const {
    return impl ? impl->config.width : 0;
}

uint32_t Window::height() const {
    return impl ? impl->config.height : 0;
}

void* Window::nativeHandle() {
    return impl ? reinterpret_cast<void*>((uintptr_t)impl->window) : nullptr;
}

void Window::setFullscreen(bool fullscreen) {
    if (!impl || !impl->display || !impl->window) {
        impl->config.fullscreen = fullscreen;
        return;
    }
    impl->config.fullscreen = fullscreen;
    impl->fullscreen = fullscreen;
    Atom wmState = XInternAtom(impl->display, "_NET_WM_STATE", False);
    Atom fullscreenAtom = XInternAtom(impl->display, "_NET_WM_STATE_FULLSCREEN", False);
    XEvent event = {};
    event.xclient.type = ClientMessage;
    event.xclient.window = impl->window;
    event.xclient.message_type = wmState;
    event.xclient.format = 32;
    event.xclient.data.l[0] = fullscreen ? 1 : 0;
    event.xclient.data.l[1] = (long)fullscreenAtom;
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 1;
    XSendEvent(impl->display, DefaultRootWindow(impl->display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(impl->display);
}

bool Window::isFullscreen() const {
    return impl ? impl->fullscreen || impl->config.fullscreen : false;
}

} // namespace ion
