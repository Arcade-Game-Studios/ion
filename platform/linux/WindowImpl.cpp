#include <ion/platform/Window.hpp>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstring>
#include <mutex>

namespace ion {

namespace {

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
