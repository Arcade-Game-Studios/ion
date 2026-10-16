#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ion {

//
// Decodes an image file into tightly packed RGBA8 pixels, top-to-bottom.
// Supported formats (detected from the file contents):
//
//   PNG  every color type and bit depth (1/2/4/8/16), palettes, tRNS
//        transparency and Adam7 interlacing. 16-bit samples are reduced
//        to 8 bits.
//   BMP  uncompressed 24/32-bit, including BI_BITFIELDS, top-down or
//        bottom-up.
//   TGA  24/32-bit true color and 8-bit grayscale, raw or RLE.
//
// Returns false for unsupported formats or corrupt data.
//
bool loadImage(const std::string& path, uint32_t& width, uint32_t& height,
               std::vector<uint8_t>& rgba);

// Same as loadImage but decodes from an in-memory image file.
bool loadImageFromMemory(const uint8_t* data, size_t size, uint32_t& width,
                         uint32_t& height, std::vector<uint8_t>& rgba);

// Encodes RGBA8 pixels (top-to-bottom) as a PNG file in memory. Returns an
// empty vector on failure.
std::vector<uint8_t> encodePng(uint32_t width, uint32_t height, const uint8_t* rgba);

// Writes RGBA8 pixels to a PNG file (e.g. screenshots, generated textures).
bool savePng(const std::string& path, uint32_t width, uint32_t height, const uint8_t* rgba);

// Reverses row order in place (convert between top-down and bottom-up,
// e.g. for pixels read back from OpenGL).
void flipImageVertically(uint32_t width, uint32_t height, uint8_t* rgba);

// Multiplies RGB by alpha in place, for premultiplied-alpha blending.
void premultiplyAlpha(uint32_t width, uint32_t height, uint8_t* rgba);

} // namespace ion
