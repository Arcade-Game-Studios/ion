#include <ion/platform/Window.hpp>

namespace ion {

struct Window::Impl {
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
    return false;
}

void Window::destroy() {
    if (impl) {
        impl->open = false;
    }
}

bool Window::isOpen() const {
    return false;
}

void Window::close() {
}

void Window::pollEvents() {
}

uint32_t Window::width() const {
    return impl ? impl->config.width : 0;
}

uint32_t Window::height() const {
    return impl ? impl->config.height : 0;
}

void* Window::nativeHandle() {
    return nullptr;
}

} // namespace ion
