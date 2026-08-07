#include <ion/platform/Window.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ion {

namespace {

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
