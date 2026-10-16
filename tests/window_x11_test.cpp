// Drives the X11 window backend with synthetic X events. Needs a display
// (CI runs it under Xvfb); without one it reports "skipped" and passes.

#include <ion/platform/Input.hpp>
#include <ion/platform/Window.hpp>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

static int failures = 0;

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            failures++;                                                              \
        }                                                                            \
    } while (0)

namespace {

Display* display(ion::Window& w) { return static_cast<Display*>(w.nativeDisplay()); }
::Window handle(ion::Window& w) { return (::Window)reinterpret_cast<uintptr_t>(w.nativeHandle()); }

void sendKey(ion::Window& w, KeySym keysym, bool press, unsigned int state = 0) {
    Display* d = display(w);
    XEvent e = {};
    e.xkey.type = press ? KeyPress : KeyRelease;
    e.xkey.display = d;
    e.xkey.window = handle(w);
    e.xkey.root = DefaultRootWindow(d);
    e.xkey.keycode = XKeysymToKeycode(d, keysym);
    e.xkey.state = state;
    e.xkey.time = CurrentTime;
    e.xkey.same_screen = True;
    XSendEvent(d, handle(w), True, press ? KeyPressMask : KeyReleaseMask, &e);
    XSync(d, False);
}

void sendFocusOut(ion::Window& w) {
    XEvent e = {};
    e.xfocus.type = FocusOut;
    e.xfocus.display = display(w);
    e.xfocus.window = handle(w);
    e.xfocus.mode = NotifyNormal;
    e.xfocus.detail = NotifyAncestor;
    XSendEvent(display(w), handle(w), True, FocusChangeMask, &e);
    XSync(display(w), False);
}

// Pumps events until pred() holds or ~1 second passes.
template <typename Pred>
bool pumpUntil(ion::Window& w, Pred pred) {
    for (int i = 0; i < 100; i++) {
        w.pollEvents();
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

} // namespace

int main() {
    const char* env = std::getenv("DISPLAY");
    if (!env || !*env) {
        std::printf("window_x11_test skipped (no DISPLAY)\n");
        return 0;
    }

    ion::WindowConfig config;
    config.title = "Ion X11 test \xC3\xA9"; // non-ASCII (UTF-8 e-acute)
    config.width = 320;
    config.height = 200;
    int resizeCalls = 0;
    config.onResize = [&](uint32_t, uint32_t) { resizeCalls++; };
    ion::Window window(config);
    if (!window.create()) {
        std::printf("window_x11_test skipped (cannot open display)\n");
        return 0;
    }
    CHECK(window.isOpen());
    CHECK(window.title() == config.title);

    // UTF-8 title lands in _NET_WM_NAME.
    window.setTitle("Renamed \xE2\x9C\x93");
    CHECK(window.title() == "Renamed \xE2\x9C\x93");
    {
        Display* d = display(window);
        Atom netWmName = XInternAtom(d, "_NET_WM_NAME", False);
        Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
        Atom type;
        int format;
        unsigned long count, after;
        unsigned char* data = nullptr;
        XGetWindowProperty(d, handle(window), netWmName, 0, 1024, False, utf8, &type,
                           &format, &count, &after, &data);
        CHECK(data && std::string((char*)data, count) == "Renamed \xE2\x9C\x93");
        if (data) {
            XFree(data);
        }
        XClassHint hint;
        CHECK(XGetClassHint(d, handle(window), &hint));
        CHECK(std::string(hint.res_name) == "Ion Engine");
        XFree(hint.res_name);
        XFree(hint.res_class);
    }

    // Resizable windows can shrink below their initial size.
    {
        XSizeHints hints;
        long supplied = 0;
        CHECK(XGetWMNormalHints(display(window), handle(window), &hints, &supplied));
        CHECK((hints.flags & PMaxSize) == 0);
        CHECK(hints.min_width <= 1);
    }

    // Key press + typed text with shift.
    sendKey(window, XK_a, true, ShiftMask);
    CHECK(pumpUntil(window, [] { return ion::input::isKeyDown(ion::Key::A); }));
    CHECK(ion::input::isKeyPressed(ion::Key::A));
    CHECK(ion::input::isAnyKeyPressed());
    CHECK(ion::input::textInput() == "A");
    ion::input::update();
    CHECK(ion::input::textInput().empty());
    CHECK(!ion::input::isKeyPressed(ion::Key::A));
    CHECK(ion::input::isKeyDown(ion::Key::A));

    // Ctrl chords are shortcuts, not text.
    sendKey(window, XK_c, true, ControlMask);
    CHECK(pumpUntil(window, [] { return ion::input::isKeyDown(ion::Key::C); }));
    CHECK(ion::input::textInput().empty());
    ion::input::update();

    // Losing focus releases held keys.
    sendFocusOut(window);
    CHECK(pumpUntil(window, [] { return !ion::input::isKeyDown(ion::Key::A); }));
    CHECK(ion::input::isKeyReleased(ion::Key::A));
    CHECK(!ion::input::isKeyDown(ion::Key::C));
    ion::input::update();

    // Resizing updates the size and fires the callback once per change.
    XResizeWindow(display(window), handle(window), 400, 300);
    XSync(display(window), False);
    CHECK(pumpUntil(window, [&] { return window.width() == 400 && window.height() == 300; }));
    int callsAfterResize = resizeCalls;
    CHECK(callsAfterResize >= 1);
    // A move alone must not report a resize.
    XMoveWindow(display(window), handle(window), 50, 60);
    XSync(display(window), False);
    pumpUntil(window, [] { return false; });
    CHECK(resizeCalls == callsAfterResize);

    // Non-resizable windows advertise a fixed size.
    ion::WindowConfig fixedConfig;
    fixedConfig.width = 200;
    fixedConfig.height = 100;
    fixedConfig.resizable = false;
    ion::Window fixed(fixedConfig);
    CHECK(fixed.create());
    {
        XSizeHints hints;
        long supplied = 0;
        CHECK(XGetWMNormalHints(display(fixed), handle(fixed), &hints, &supplied));
        CHECK((hints.flags & PMaxSize) != 0);
        CHECK(hints.min_width == 200 && hints.max_width == 200);
        CHECK(hints.min_height == 100 && hints.max_height == 100);
    }
    fixed.destroy();

    // Methods on a moved-from window must not crash.
    ion::Window moved(std::move(window));
    window.setFullscreen(true);
    window.setTitle("ignored");
    CHECK(!window.isOpen());
    CHECK(moved.isOpen());
    moved.destroy();

    if (failures == 0) {
        std::printf("All X11 window tests passed\n");
        return 0;
    }
    std::printf("%d X11 window test(s) FAILED\n", failures);
    return 1;
}
