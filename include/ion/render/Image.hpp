#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ion {

//
// Decodes an image file into tightly packed RGBA8 pixels, top-to-bottom.
// PNG (8-bit gray, RGB, palette, gray+alpha, RGBA) is supported. Returns
// false for unsupported formats or corrupt data.
//
bool loadImage(const std::string& path, uint32_t& width, uint32_t& height,
               std::vector<uint8_t>& rgba);

// Same as loadImage but decodes from an in-memory image file.
bool loadImageFromMemory(const uint8_t* data, size_t size, uint32_t& width,
                         uint32_t& height, std::vector<uint8_t>& rgba);

} // namespace ion
