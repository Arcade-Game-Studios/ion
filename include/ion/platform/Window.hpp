#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ion {

struct WindowConfig {
    std::string title = "Ion Engine";
    std::string appName = "Ion Engine";
    std::string iconPath = "";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool resizable = true;
    bool fullscreen = false;

    std::function<void(uint32_t width, uint32_t height)> onResize;
};

class Window {
public:
    Window();
    explicit Window(const WindowConfig& config);
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window();

    bool create();
    void destroy();

    bool isOpen() const;
    void close();

    void pollEvents();

    uint32_t width() const;
    uint32_t height() const;

    void setFullscreen(bool fullscreen);
    bool isFullscreen() const;

    void* nativeHandle();

private:
    class Impl;
    Impl* impl = nullptr;
};

}