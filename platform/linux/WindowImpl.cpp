#include <ion/platform/Window.hpp>
#include <ion/platform/Input.hpp>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cstring>
#include <mutex>
#include <string>

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

// <X11/X.h> defines Button4/Button5 as macros, so MouseButton::Button4 can't
// be spelled here; derive the side buttons from their position after Middle.
MouseButton sideMouseButton(bool forward) {
    return static_cast<MouseButton>(static_cast<int>(MouseButton::Middle) + (forward ? 2 : 1));
}

// Unicode code point for a keysym produced by XLookupString (0 if none).
uint32_t keysymToCodepoint(KeySym keysym) {
    if ((keysym >= 0x20 && keysym <= 0x7E) || (keysym >= 0xA0 && keysym <= 0xFF)) {
        return (uint32_t)keysym; // Latin-1 keysyms equal their code points
    }
    if ((keysym & 0xFF000000) == 0x01000000) {
        return (uint32_t)(keysym & 0x00FFFFFF); // direct Unicode keysyms
    }
    switch (keysym) {
    case XK_KP_Space: return ' ';
    case XK_KP_Equal: return '=';
    case XK_KP_Multiply: return '*';
    case XK_KP_Add: return '+';
    case XK_KP_Subtract: return '-';
    case XK_KP_Decimal: return '.';
    case XK_KP_Divide: return '/';
    default: break;
    }
    if (keysym >= XK_KP_0 && keysym <= XK_KP_9) {
        return (uint32_t)('0' + (keysym - XK_KP_0));
    }
    return 0;
}

void setSizeHints(Display* display, ::Window window, const WindowConfig& config) {
    XSizeHints* hints = XAllocSizeHints();
    if (!hints) {
        return;
    }
    if (config.resizable) {
        hints->flags = PMinSize;
        hints->min_width = 1;
        hints->min_height = 1;
    } else {
        // Equal min and max is how X11 window managers lock the size.
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = (int)config.width;
        hints->min_height = hints->max_height = (int)config.height;
    }
    XSetWMNormalHints(display, window, hints);
    XFree(hints);
}

// Sets both the legacy WM_NAME and the UTF-8 _NET_WM_NAME so non-ASCII
// titles display correctly under modern window managers.
void applyTitle(Display* display, ::Window window, const std::string& title) {
    XStoreName(display, window, title.c_str());
    Atom netWmName = XInternAtom(display, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
    XChangeProperty(display, window, netWmName, utf8, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title.c_str()), (int)title.size());
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
                       PointerMotionMask | FocusChangeMask;

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

    applyTitle(display, xw, impl->config.title);

    // Lets taskbars and window rules identify and group the game's windows.
    XClassHint* classHint = XAllocClassHint();
    if (classHint) {
        std::string appName = impl->config.appName.empty() ? "ion" : impl->config.appName;
        classHint->res_name = const_cast<char*>(appName.c_str());
        classHint->res_class = const_cast<char*>(appName.c_str());
        XSetClassHint(display, xw, classHint);
        XFree(classHint);
    }

    Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, xw, &wmDelete, 1);

    setSizeHints(display, xw, impl->config);

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
        case ConfigureNotify: {
            // Also sent for moves and restacking; only report real resizes.
            uint32_t w = (uint32_t)event.xconfigure.width;
            uint32_t h = (uint32_t)event.xconfigure.height;
            if (w == impl->config.width && h == impl->config.height) {
                break;
            }
            impl->config.width = w;
            impl->config.height = h;
            if (impl->config.onResize) {
                impl->config.onResize(w, h);
            }
            break;
        }
        case FocusOut:
            input::platformReleaseAll();
            break;
        case KeyPress: {
            Key key = keyFromKeysym(XLookupKeysym(&event.xkey, 0));
            if (key != Key::Unknown) {
                input::platformKeyDown(key);
            }
            // Shift/layout-aware text. Ctrl/Alt chords are shortcuts.
            if (!(event.xkey.state & (ControlMask | Mod1Mask))) {
                char text[32];
                KeySym typed = NoSymbol;
                XLookupString(&event.xkey, text, sizeof(text), &typed, nullptr);
                uint32_t codepoint = keysymToCodepoint(typed);
                if (codepoint != 0) {
                    input::platformTextInput(codepoint);
                }
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
            case 8: // "back" side button
                if (down) input::platformMouseDown(sideMouseButton(false));
                else input::platformMouseUp(sideMouseButton(false));
                break;
            case 9: // "forward" side button
                if (down) input::platformMouseDown(sideMouseButton(true));
                else input::platformMouseUp(sideMouseButton(true));
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

void* Window::nativeDisplay() {
    return impl ? (void*)impl->display : nullptr;
}

void Window::setTitle(const std::string& title) {
    if (!impl) {
        return;
    }
    impl->config.title = title;
    if (impl->display && impl->window) {
        applyTitle(impl->display, impl->window, title);
        XFlush(impl->display);
    }
}

const std::string& Window::title() const {
    static const std::string empty;
    return impl ? impl->config.title : empty;
}

void Window::setFullscreen(bool fullscreen) {
    if (!impl) {
        return;
    }
    if (!impl->display || !impl->window) {
        impl->config.fullscreen = fullscreen; // applied by create()
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
