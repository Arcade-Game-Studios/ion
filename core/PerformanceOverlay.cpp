#include <ion/render/PerformanceOverlay.hpp>

#include <ion/core/Memory.hpp>
#include <ion/core/Timer.hpp>
#include <ion/render/Color.hpp>

#include <algorithm>
#include <cstdio>
#include <string>

namespace ion {

bool PerformanceOverlay::initialize(Renderer* renderer, uint32_t viewportWidth,
                                    uint32_t viewportHeight) {
    shutdown();
    if (!renderer) {
        return false;
    }
    renderer_ = renderer;

    if (!batch_.initialize(renderer_, nullptr, 256)) {
        shutdown();
        return false;
    }
    if (!font_.initialize(renderer_)) {
        shutdown();
        return false;
    }

    camera_.setZoom(1.0f);
    camera_.setPosition(Vector2((float)viewportWidth * 0.5f,
                                (float)viewportHeight * 0.5f));
    camera_.setViewport(viewportWidth, viewportHeight);
    initialized_ = true;
    return true;
}

void PerformanceOverlay::shutdown() {
    font_.shutdown();
    batch_.shutdown();
    renderer_ = nullptr;
    initialized_ = false;
}

bool PerformanceOverlay::isInitialized() const {
    return initialized_;
}

void PerformanceOverlay::setEnabled(bool enabled) {
    enabled_ = enabled;
}

bool PerformanceOverlay::isEnabled() const {
    return enabled_;
}

void PerformanceOverlay::setViewport(uint32_t width, uint32_t height) {
    camera_.setViewport(width, height);
    camera_.setPosition(Vector2((float)width * 0.5f, (float)height * 0.5f));
}

void PerformanceOverlay::setFontSize(float glyphHeight) {
    if (glyphHeight > 0.0f) {
        fontSize_ = glyphHeight;
    }
}

void PerformanceOverlay::setLine(const std::string& label, const std::string& value) {
    for (auto& line : extraLines_) {
        if (line.first == label) {
            line.second = value;
            return;
        }
    }
    extraLines_.emplace_back(label, value);
}

void PerformanceOverlay::removeLine(const std::string& label) {
    for (size_t i = 0; i < extraLines_.size(); i++) {
        if (extraLines_[i].first == label) {
            extraLines_.erase(extraLines_.begin() + (long)i);
            return;
        }
    }
}

void PerformanceOverlay::clearLines() {
    extraLines_.clear();
}

std::string PerformanceOverlay::statsText_() const {
    const RendererStats& stats = renderer_->stats();
    char buffer[320];
    int n = std::snprintf(buffer, sizeof(buffer),
                          "FPS: %.0f\nFrame: %.2f ms (worst %.2f)\n"
                          "Draw calls: %u\nTriangles: %u\nVertices: %u",
                          fps_, lastFrameMs_, worstFrameMs_, stats.drawCalls,
                          stats.triangles, stats.vertices);
    std::string text(buffer, n > 0 ? std::min((size_t)n, sizeof(buffer) - 1) : 0);
    size_t bytes = Memory::allocatedBytes();
    if (bytes > 0) { // zero when ION_MEMORY_TRACKING is off
        std::snprintf(buffer, sizeof(buffer), "\nHeap: %.1f MB (%zu allocs)",
                      (double)bytes / (1024.0 * 1024.0), Memory::allocationCount());
        text += buffer;
    }
    for (const auto& line : extraLines_) {
        text += "\n" + line.first + ": " + line.second;
    }
    return text;
}

void PerformanceOverlay::render() {
    if (!initialized_ || !enabled_) {
        return;
    }

    float deltaTime = frameTimer_.tick();
    if (hasPreviousFrame_) {
        lastFrameMs_ = deltaTime * 1000.0f;
        // Worst frame over a rolling one-second window: spikes are what
        // players notice, and averages hide them.
        windowWorstMs_ = std::max(windowWorstMs_, lastFrameMs_);
        windowElapsed_ += deltaTime;
        if (windowElapsed_ >= 1.0f) {
            worstFrameMs_ = windowWorstMs_;
            windowWorstMs_ = 0.0f;
            windowElapsed_ = 0.0f;
        }
        float instantFps = (deltaTime > 0.0f) ? (1.0f / deltaTime) : 0.0f;
        if (fps_ <= 0.0f) {
            fps_ = instantFps;
        } else {
            fps_ += (instantFps - fps_) * 0.05f; // EMA smoothing
        }
    } else {
        hasPreviousFrame_ = true;
    }

    std::string text = statsText_();
    Vector2 textSize = font_.measure(text, fontSize_);
    const float pad = 8.0f;
    const float margin = 12.0f;

    // Text block grows upward from its bottom-left corner; top of the block
    // is anchored just below the window's top edge.
    Vector2 textPos(margin + pad,
                    (float)camera_.viewportHeight() - margin - textSize.y - pad);
    Vector2 boxPos(margin, textPos.y - pad);
    Vector2 boxSize(textSize.x + pad * 2.0f, textSize.y + pad * 2.0f);

    batch_.begin(camera_);
    batch_.drawRect(boxPos, boxSize, Color(0.0f, 0.0f, 0.0f, 0.6f));
    font_.draw(batch_, text, textPos, fontSize_, Color::white());
    batch_.end();
}

} // namespace ion
