#pragma once

#include <ion/math/Vector2.hpp>
#include <ion/render/Color.hpp>
#include <ion/render/Texture.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ion {

class Renderer;

//
// Minimal SVG parser and rasterizer for 2D art. Supports a practical subset
// of SVG suitable for game assets:
//
//   - Root <svg> with width/height or viewBox.
//   - <rect> (x, y, width, height, rx, ry), <circle>, <ellipse>, <line>,
//     <polyline>, <polygon> and <path>.
//   - <path> commands: M, L, H, V, C, S, Q, T, A, Z plus their lowercase
//     relative variants, with repeated coordinate sets.
//   - <g> groups that inherit fill/stroke/stroke-width/opacity and
//     transforms. Unknown containers (<defs>, gradients, ...) are skipped.
//   - Paint: hex colors (#rgb, #rrggbb, #rrggbbaa), named colors or "none".
//   - stroke-width, fill-opacity, stroke-opacity, opacity and the
//     style="..." attribute.
//   - transform="translate() scale() rotate() matrix() skewX() skewY()"
//     on shapes and groups.
//
// The document is rasterized into an RGBA8 pixel buffer (rows top-to-bottom,
// matching the engine texture convention) with 2x2 supersampling for
// anti-aliased edges. The result can be uploaded with
// Renderer::createTexture() and used as a SpriteRegion.
//
// Usage:
//   ion::SvgImage image;
//   if (image.parse(shipSvg)) {
//       ion::Texture tex = image.createTexture(renderer);
//       ion::SpriteRegion region = ion::SpriteRegion::full(tex);
//   }
//
class SvgImage {
public:
    SvgImage();
    ~SvgImage();
    SvgImage(SvgImage&& other) noexcept;
    SvgImage& operator=(SvgImage&& other) noexcept;
    SvgImage(const SvgImage&) = delete;
    SvgImage& operator=(const SvgImage&) = delete;

    // Parses SVG markup. Returns false on a parse error; the document is
    // left unchanged on failure.
    bool parse(const std::string& svg);

    // Parses an SVG file from disk. Returns false if the file cannot be
    // read or the markup is invalid.
    bool parseFile(const std::string& path);

    bool isValid() const;

    // Document size in pixels, derived from the <svg> width/height or
    // viewBox. If neither is present the bounding box of the shapes is used.
    uint32_t width() const;
    uint32_t height() const;

    // Rasterizes the document into an RGBA8 buffer of width()*height()*4
    // bytes. Returns an empty vector on failure or if the document has no
    // size.
    std::vector<uint8_t> rasterize() const;

    // Rasterizes and uploads the result to the renderer as a texture.
    Texture createTexture(Renderer& renderer) const;

private:
    struct Shape;
    std::vector<Shape> shapes_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool valid_ = false;
};

// One-shot convenience helper: parses and rasterizes SVG markup into an
// RGBA8 buffer. On success sets outWidth/outHeight and returns the pixel
// data; returns an empty vector on failure.
std::vector<uint8_t> rasterizeSvg(const std::string& svg, uint32_t& outWidth,
                                  uint32_t& outHeight);

} // namespace ion
