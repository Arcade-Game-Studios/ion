#include <ion/render/TextureAtlas.hpp>

#include <algorithm>

namespace ion {

void TextureAtlas::setTexture(const Texture& texture) {
    texture_ = texture;
    regions_.clear();
    order_.clear();
}

bool TextureAtlas::addRegion(const std::string& name, uint32_t x, uint32_t y,
                             uint32_t width, uint32_t height) {
    if (name.empty() || width == 0 || height == 0) {
        return false;
    }
    if (!texture_.isValid()) {
        return false;
    }
    if (regions_.count(name) > 0) {
        return false;
    }
    SpriteRegion region;
    region.texture = texture_;
    region.x = x;
    region.y = y;
    region.width = width;
    region.height = height;
    region.computeUV();
    regions_.emplace(name, region);
    order_.push_back(name);
    return true;
}

bool TextureAtlas::addGrid(const std::string& namePrefix, uint32_t columns,
                           uint32_t rows) {
    if (!texture_.isValid() || columns == 0 || rows == 0) {
        return false;
    }
    uint32_t cellWidth = texture_.desc.width / columns;
    uint32_t cellHeight = texture_.desc.height / rows;
    return addGrid(namePrefix, columns, rows, cellWidth, cellHeight);
}

bool TextureAtlas::addGrid(const std::string& namePrefix, uint32_t columns,
                           uint32_t rows, uint32_t cellWidth,
                           uint32_t cellHeight) {
    if (namePrefix.empty() || columns == 0 || rows == 0 || cellWidth == 0 ||
        cellHeight == 0) {
        return false;
    }
    if (!texture_.isValid()) {
        return false;
    }
    bool ok = true;
    for (uint32_t row = 0; row < rows; row++) {
        for (uint32_t col = 0; col < columns; col++) {
            std::string name = namePrefix + "_" + std::to_string(
                                                  row * columns + col);
            if (!addRegion(name, col * cellWidth, row * cellHeight,
                           cellWidth, cellHeight)) {
                ok = false;
            }
        }
    }
    return ok;
}

const SpriteRegion* TextureAtlas::find(const std::string& name) const {
    auto it = regions_.find(name);
    return it != regions_.end() ? &it->second : nullptr;
}

const std::vector<std::string>& TextureAtlas::names() const {
    return order_;
}

const Texture& TextureAtlas::texture() const {
    return texture_;
}

size_t TextureAtlas::regionCount() const {
    return regions_.size();
}

} // namespace ion
