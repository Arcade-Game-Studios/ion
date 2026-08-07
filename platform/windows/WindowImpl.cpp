#include <ion/platform/Window.hpp>
#include <ion/platform/Input.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

namespace ion {

namespace {

Key keyFromVirtualKey(unsigned int vk) {
    if (vk >= 'A' && vk <= 'Z') {
        return static_cast<Key>(static_cast<int>(Key::A) + (vk - 'A'));
    }
    if (vk >= '0' && vk <= '9') {
        return static_cast<Key>(static_cast<int>(Key::Num0) + (vk - '0'));
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return static_cast<Key>(static_cast<int>(Key::F1) + (vk - VK_F1));
    }
    switch (vk) {
    case VK_ESCAPE: return Key::Escape;
    case VK_TAB: return Key::Tab;
    case VK_CAPITAL: return Key::CapsLock;
    case VK_SPACE: return Key::Space;
    case VK_RETURN: return Key::Enter;
    case VK_BACK: return Key::Backspace;
    case VK_DELETE: return Key::Delete;
    case VK_INSERT: return Key::Insert;
    case VK_HOME: return Key::Home;
    case VK_END: return Key::End;
    case VK_PRIOR: return Key::PageUp;
    case VK_NEXT: return Key::PageDown;
    case VK_LEFT: return Key::ArrowLeft;
    case VK_RIGHT: return Key::ArrowRight;
    case VK_UP: return Key::ArrowUp;
    case VK_DOWN: return Key::ArrowDown;
    case VK_LSHIFT: return Key::LeftShift;
    case VK_RSHIFT: return Key::RightShift;
    case VK_LCONTROL: return Key::LeftControl;
    case VK_RCONTROL: return Key::RightControl;
    case VK_LMENU: return Key::LeftAlt;
    case VK_RMENU: return Key::RightAlt;
    case VK_LWIN: return Key::LeftSuper;
    case VK_RWIN: return Key::RightSuper;
    case VK_APPS: return Key::Menu;
    case VK_OEM_1: return Key::Semicolon;
    case VK_OEM_7: return Key::Apostrophe;
    case VK_OEM_COMMA: return Key::Comma;
    case VK_OEM_PERIOD: return Key::Period;
    case VK_OEM_2: return Key::Slash;
    case VK_OEM_5: return Key::Backslash;
    case VK_OEM_MINUS: return Key::Minus;
    case VK_OEM_PLUS: return Key::Equal;
    case VK_OEM_4: return Key::LeftBracket;
    case VK_OEM_6: return Key::RightBracket;
    case VK_OEM_3: return Key::Backtick;
    case VK_NUMPAD0: return Key::Keypad0;
    case VK_NUMPAD1: return Key::Keypad1;
    case VK_NUMPAD2: return Key::Keypad2;
    case VK_NUMPAD3: return Key::Keypad3;
    case VK_NUMPAD4: return Key::Keypad4;
    case VK_NUMPAD5: return Key::Keypad5;
    case VK_NUMPAD6: return Key::Keypad6;
    case VK_NUMPAD7: return Key::Keypad7;
    case VK_NUMPAD8: return Key::Keypad8;
    case VK_NUMPAD9: return Key::Keypad9;
    case VK_DECIMAL: return Key::KeypadDecimal;
    case VK_DIVIDE: return Key::KeypadDivide;
    case VK_MULTIPLY: return Key::KeypadMultiply;
    case VK_SUBTRACT: return Key::KeypadSubtract;
    case VK_ADD: return Key::KeypadAdd;
    default: return Key::Unknown;
    }
}

MouseButton mouseButtonFromWParam(WPARAM wParam) {
    switch (HIWORD(wParam)) {
    case XBUTTON1: return MouseButton::Button4;
    case XBUTTON2: return MouseButton::Button5;
    default: return MouseButton::None;
    }
}

struct Window::Impl {
    HWND hwnd = nullptr;
    bool open = false;
    bool fullscreen = false;
    WINDOWPLACEMENT savedPlacement = {};
    WindowConfig config;
};

Window::Impl* implFromHandle(HWND hwnd) {
    return reinterpret_cast<Window::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window::Impl* impl = implFromHandle(hwnd);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        impl = reinterpret_cast<Window::Impl*>(cs->lpCreateParams);
        return 0;
    }
    case WM_SIZE: {
        if (!impl) {
            break;
        }
        impl->config.width = (uint32_t)LOWORD(lParam);
        impl->config.height = (uint32_t)HIWORD(lParam);
        if (impl->config.onResize) {
            impl->config.onResize(impl->config.width, impl->config.height);
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        Key key = keyFromVirtualKey((unsigned int)wParam);
        if (key != Key::Unknown) {
            input::platformKeyDown(key);
        }
        return 0;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        Key key = keyFromVirtualKey((unsigned int)wParam);
        if (key != Key::Unknown) {
            input::platformKeyUp(key);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
        input::platformMouseDown(MouseButton::Left);
        return 0;
    case WM_LBUTTONUP:
        input::platformMouseUp(MouseButton::Left);
        return 0;
    case WM_RBUTTONDOWN:
        input::platformMouseDown(MouseButton::Right);
        return 0;
    case WM_RBUTTONUP:
        input::platformMouseUp(MouseButton::Right);
        return 0;
    case WM_MBUTTONDOWN:
        input::platformMouseDown(MouseButton::Middle);
        return 0;
    case WM_MBUTTONUP:
        input::platformMouseUp(MouseButton::Middle);
        return 0;
    case WM_XBUTTONDOWN:
        input::platformMouseDown(mouseButtonFromWParam(wParam));
        return TRUE;
    case WM_XBUTTONUP:
        input::platformMouseUp(mouseButtonFromWParam(wParam));
        return TRUE;
    case WM_MOUSEMOVE:
        input::platformMouseMove((float)(int16_t)LOWORD(lParam),
                                 (float)(int16_t)HIWORD(lParam));
        return 0;
    case WM_MOUSEWHEEL:
        input::platformMouseScroll(0.0f,
                                   (float)GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f);
        return 0;
    case WM_MOUSEHWHEEL:
        input::platformMouseScroll((float)GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f,
                                   0.0f);
        return 0;
    case WM_CLOSE:
        if (impl) {
            impl->open = false;
        }
        return 0;
    case WM_DESTROY:
        if (impl) {
            impl->open = false;
            impl->hwnd = nullptr;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
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
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    static wchar_t className[] = L"IonEngineWindow";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = windowProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = className;
        if (!RegisterClassExW(&wc)) {
            return false;
        }
        registered = true;
    }

    int width = (int)impl->config.width;
    int height = (int)impl->config.height;
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!impl->config.resizable) {
        style &= ~WS_MAXIMIZEBOX;
        style &= ~WS_THICKFRAME;
    }
    RECT rect = {0, 0, width, height};
    AdjustWindowRectEx(&rect, style, FALSE, 0);

    HWND hwnd = CreateWindowExW(
        0, className,
        std::wstring(impl->config.title.begin(), impl->config.title.end()).c_str(),
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, impl.get());

    if (!hwnd) {
        return false;
    }

    impl->hwnd = hwnd;
    impl->open = true;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    if (impl->config.fullscreen) {
        setFullscreen(true);
    }

    return true;
}

void Window::destroy() {
    if (!impl) {
        return;
    }
    if (impl->hwnd) {
        DestroyWindow(impl->hwnd);
        impl->hwnd = nullptr;
    }
    impl->open = false;
}

bool Window::isOpen() const {
    return impl && impl->open && impl->hwnd && IsWindow(impl->hwnd);
}

void Window::close() {
    if (impl && impl->hwnd) {
        impl->open = false;
        PostMessageW(impl->hwnd, WM_CLOSE, 0, 0);
    }
}

void Window::pollEvents() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ion::input::pollGamepads();
}

uint32_t Window::width() const {
    if (impl && impl->hwnd) {
        RECT r;
        if (GetClientRect(impl->hwnd, &r)) {
            return (uint32_t)(r.right - r.left);
        }
    }
    return impl ? impl->config.width : 0;
}

uint32_t Window::height() const {
    if (impl && impl->hwnd) {
        RECT r;
        if (GetClientRect(impl->hwnd, &r)) {
            return (uint32_t)(r.bottom - r.top);
        }
    }
    return impl ? impl->config.height : 0;
}

void* Window::nativeHandle() {
    return impl ? (void*)impl->hwnd : nullptr;
}

void Window::setFullscreen(bool fullscreen) {
    if (!impl || !impl->hwnd) {
        impl->config.fullscreen = fullscreen;
        return;
    }
    impl->config.fullscreen = fullscreen;
    if (fullscreen && !impl->fullscreen) {
        GetWindowPlacement(impl->hwnd, &impl->savedPlacement);
        HMONITOR monitor = MonitorFromWindow(impl->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info = {};
        info.cbSize = sizeof(MONITORINFO);
        if (GetMonitorInfoW(monitor, &info)) {
            SetWindowLongPtrW(impl->hwnd, GWL_STYLE, (LONG_PTR)WS_POPUP);
            SetWindowPos(impl->hwnd, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                         info.rcMonitor.right - info.rcMonitor.left,
                         info.rcMonitor.bottom - info.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            impl->fullscreen = true;
        }
    } else if (!fullscreen && impl->fullscreen) {
        SetWindowLongPtrW(impl->hwnd, GWL_STYLE, (LONG_PTR)WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(impl->hwnd, &impl->savedPlacement);
        SetWindowPos(impl->hwnd, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                         SWP_FRAMECHANGED);
        impl->fullscreen = false;
    }
}

bool Window::isFullscreen() const {
    return impl ? impl->fullscreen : false;
}

} // namespace ion
