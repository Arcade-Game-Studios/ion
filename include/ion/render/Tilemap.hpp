#pragma once

#include <ion/math/Vector2.hpp>
#include <ion/render/Camera2D.hpp>
#include <ion/render/SpriteBatch.hpp>
#include <ion/render/Texture.hpp>

#include <cstdint>
#include <vector>

namespace ion {

struct TilemapDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t tileSize = 32;
    // Tileset texture containing a uniform grid of tileSize x tileSize tiles.
    Texture tileset;
    // Number of tile columns in the tileset texture.
    uint32_t tilesAcross = 1;
};

//
// A 2D grid of tiles referencing cells of a tileset texture. Tiles are
// stored by index into the tileset grid; a tile index of -1 means empty.
// draw() culls to the visible range of the camera before drawing, which
// keeps the cost proportional to the screen area rather than the map size.
//
class Tilemap {
public:
    Tilemap() = default;

    bool create(const TilemapDesc& desc);
    void destroy();

    void setTile(uint32_t x, uint32_t y, int32_t tileId);
    int32_t tile(uint32_t x, uint32_t y) const;
    bool empty(uint32_t x, uint32_t y) const;

    uint32_t width() const;
    uint32_t height() const;
    uint32_t tileSize() const;

    void draw(SpriteBatch& batch, const Camera2D& camera) const;

private:
    SpriteRegion tileRegion_(int32_t tileId) const;

    TilemapDesc desc_;
    std::vector<int32_t> tiles_;
    bool valid_ = false;
};

} // namespace ion
