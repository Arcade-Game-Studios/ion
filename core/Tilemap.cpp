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
    solidOverride_.clear();
    valid_ = false;
}

void Tilemap::fill(int32_t tileId) {
    if (valid_) {
        std::fill(tiles_.begin(), tiles_.end(), tileId);
    }
}

void Tilemap::fillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int32_t tileId) {
    if (!valid_) {
        return;
    }
    uint32_t x1 = std::min(desc_.width, x + w);
    uint32_t y1 = std::min(desc_.height, y + h);
    for (uint32_t row = y; row < y1; row++) {
        for (uint32_t col = x; col < x1; col++) {
            tiles_[(size_t)row * desc_.width + col] = tileId;
        }
    }
}

bool Tilemap::setTiles(const std::vector<int32_t>& tiles) {
    if (!valid_ || tiles.size() != tiles_.size()) {
        return false;
    }
    tiles_ = tiles;
    return true;
}

bool Tilemap::worldToTile(const Vector2& world, int32_t& x, int32_t& y) const {
    if (!valid_) {
        return false;
    }
    float ts = (float)desc_.tileSize;
    int32_t col = (int32_t)std::floor(world.x / ts);
    int32_t row = (int32_t)std::floor(world.y / ts);
    if (col < 0 || row < 0 || col >= (int32_t)desc_.width || row >= (int32_t)desc_.height) {
        return false;
    }
    x = col;
    y = row;
    return true;
}

Vector2 Tilemap::tileToWorld(int32_t x, int32_t y) const {
    return Vector2((float)x * (float)desc_.tileSize, (float)y * (float)desc_.tileSize);
}

Rect Tilemap::tileBounds(int32_t x, int32_t y) const {
    float ts = (float)desc_.tileSize;
    return Rect((float)x * ts, (float)y * ts, ts, ts);
}

Rect Tilemap::worldBounds() const {
    float ts = (float)desc_.tileSize;
    return Rect(0.0f, 0.0f, (float)desc_.width * ts, (float)desc_.height * ts);
}

void Tilemap::setSolid(int32_t tileId, bool solid) {
    if (tileId < 0) {
        return;
    }
    if ((size_t)tileId >= solidOverride_.size()) {
        solidOverride_.resize((size_t)tileId + 1, 0);
    }
    solidOverride_[(size_t)tileId] = solid ? 1 : 2;
}

bool Tilemap::solidId_(int32_t tileId) const {
    if (tileId < 0) {
        return false;
    }
    if ((size_t)tileId < solidOverride_.size() && solidOverride_[(size_t)tileId] != 0) {
        return solidOverride_[(size_t)tileId] == 1;
    }
    return true;
}

bool Tilemap::isSolidTile(int32_t x, int32_t y) const {
    if (!valid_ || x < 0 || y < 0 || x >= (int32_t)desc_.width || y >= (int32_t)desc_.height) {
        return false;
    }
    return solidId_(tiles_[(size_t)y * desc_.width + (size_t)x]);
}

bool Tilemap::isSolidAt(const Vector2& world) const {
    int32_t x, y;
    return worldToTile(world, x, y) && isSolidTile(x, y);
}

bool Tilemap::overlapsSolid(const Rect& rect) const {
    if (!valid_ || rect.isEmpty()) {
        return false;
    }
    float ts = (float)desc_.tileSize;
    // Half-open on the max edge: a box resting exactly on a tile boundary
    // does not overlap the next tile.
    int32_t x0 = (int32_t)std::floor(rect.left() / ts);
    int32_t y0 = (int32_t)std::floor(rect.bottom() / ts);
    int32_t x1 = (int32_t)std::ceil(rect.right() / ts) - 1;
    int32_t y1 = (int32_t)std::ceil(rect.top() / ts) - 1;
    for (int32_t y = y0; y <= y1; y++) {
        for (int32_t x = x0; x <= x1; x++) {
            if (isSolidTile(x, y)) {
                return true;
            }
        }
    }
    return false;
}

TileCollision Tilemap::moveAndSlide(Rect& box, const Vector2& delta) const {
    TileCollision result;
    if (!valid_) {
        box = box.translated(delta);
        result.moved = delta;
        return result;
    }
    float ts = (float)desc_.tileSize;

    // X axis: move, and if we now overlap a solid tile snap to its edge.
    if (delta.x != 0.0f) {
        Rect moved = box.translated(Vector2(delta.x, 0.0f));
        if (overlapsSolid(moved)) {
            result.hitX = true;
            if (delta.x > 0.0f) {
                float edge = std::floor(moved.right() / ts) * ts;
                moved.x = edge - box.width;
            } else {
                float edge = (std::floor(moved.left() / ts) + 1.0f) * ts;
                moved.x = edge;
            }
            if (overlapsSolid(moved)) {
                moved.x = box.x; // couldn't resolve; don't move
            }
        }
        result.moved.x = moved.x - box.x;
        box.x = moved.x;
    }

    if (delta.y != 0.0f) {
        Rect moved = box.translated(Vector2(0.0f, delta.y));
        if (overlapsSolid(moved)) {
            result.hitY = true;
            if (delta.y > 0.0f) {
                float edge = std::floor(moved.top() / ts) * ts;
                moved.y = edge - box.height;
                result.onCeiling = true;
            } else {
                float edge = (std::floor(moved.bottom() / ts) + 1.0f) * ts;
                moved.y = edge;
                result.onGround = true;
            }
            if (overlapsSolid(moved)) {
                moved.y = box.y;
            }
        }
        result.moved.y = moved.y - box.y;
        box.y = moved.y;
    }
    return result;
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
    // visibleBounds() accounts for rotation, so rotated cameras no longer
    // cull tiles that are still on screen.
    Rect view = camera.visibleBounds();
    float left = view.left();
    float right = view.right();
    float bottom = view.bottom();
    float top = view.top();

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
