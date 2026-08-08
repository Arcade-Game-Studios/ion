#include <ion/render/Tilemap.hpp>

#include <algorithm>
#include <cmath>

namespace ion {

bool Tilemap::create(const TilemapDesc& desc) {
    destroy();
    if (desc.width == 0 || desc.height == 0 || desc.tileSize == 0 ||
        !desc.tileset.isValid() || desc.tilesAcross == 0) {
        return false;
    }
    desc_ = desc;
    tiles_.assign((size_t)desc_.width * desc_.height, -1);
    valid_ = true;
    return true;
}

void Tilemap::destroy() {
    tiles_.clear();
    valid_ = false;
}

void Tilemap::setTile(uint32_t x, uint32_t y, int32_t tileId) {
    if (!valid_ || x >= desc_.width || y >= desc_.height) {
        return;
    }
    tiles_[(size_t)y * desc_.width + x] = tileId;
}

int32_t Tilemap::tile(uint32_t x, uint32_t y) const {
    if (!valid_ || x >= desc_.width || y >= desc_.height) {
        return -1;
    }
    return tiles_[(size_t)y * desc_.width + x];
}

bool Tilemap::empty(uint32_t x, uint32_t y) const {
    return tile(x, y) < 0;
}

uint32_t Tilemap::width() const {
    return desc_.width;
}

uint32_t Tilemap::height() const {
    return desc_.height;
}

uint32_t Tilemap::tileSize() const {
    return desc_.tileSize;
}

void Tilemap::draw(SpriteBatch& batch, const Camera2D& camera) const {
    if (!valid_) {
        return;
    }
    float halfW = camera.viewportWidth() * 0.5f / camera.zoom();
    float halfH = camera.viewportHeight() * 0.5f / camera.zoom();
    float left = camera.position().x - halfW;
    float right = camera.position().x + halfW;
    float bottom = camera.position().y - halfH;
    float top = camera.position().y + halfH;

    int32_t startCol = std::max(0, (int)std::floor(left / (float)desc_.tileSize));
    int32_t endCol = std::min((int32_t)desc_.width - 1,
                              (int)std::floor(right / (float)desc_.tileSize));
    int32_t startRow = std::max(0, (int)std::floor(bottom / (float)desc_.tileSize));
    int32_t endRow = std::min((int32_t)desc_.height - 1,
                              (int)std::floor(top / (float)desc_.tileSize));

    float tileSize = (float)desc_.tileSize;
    for (int32_t row = startRow; row <= endRow; row++) {
        for (int32_t col = startCol; col <= endCol; col++) {
            int32_t id = tiles_[(size_t)row * desc_.width + col];
            if (id < 0) {
                continue;
            }
            SpriteRegion region = tileRegion_(id);
            if (!region.isValid()) {
                continue;
            }
            batch.drawSprite(region, Vector2((float)col * tileSize,
                                             (float)row * tileSize),
                             Vector2(tileSize, tileSize), 0.0f,
                             {0.0f, 0.0f});
        }
    }
}

SpriteRegion Tilemap::tileRegion_(int32_t tileId) const {
    SpriteRegion region;
    region.texture = desc_.tileset;
    uint32_t texW = desc_.tileset.desc.width;
    uint32_t texH = desc_.tileset.desc.height;
    uint32_t perRow = desc_.tileset.desc.width / desc_.tileSize;
    if (perRow == 0) {
        perRow = desc_.tilesAcross;
    }
    uint32_t col = (uint32_t)tileId % perRow;
    uint32_t row = (uint32_t)tileId / perRow;
    region.x = col * desc_.tileSize;
    region.y = row * desc_.tileSize;
    region.width = desc_.tileSize;
    region.height = desc_.tileSize;
    if (region.x + region.width > texW || region.y + region.height > texH) {
        return SpriteRegion();
    }
    region.computeUV();
    return region;
}

} // namespace ion
