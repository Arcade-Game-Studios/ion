#pragma once

#include <ion/math/Geometry.hpp>
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
// Tile (x, y) covers the world rect [x*tileSize, (x+1)*tileSize) x
// [y*tileSize, (y+1)*tileSize); row 0 is at the bottom (+y up).
//
// Collision: by default every non-empty tile is solid; setSolid() changes
// that per tile id.
//
struct TileCollision {
    Vector2 moved;          // displacement actually applied
    bool hitX = false;      // blocked horizontally
    bool hitY = false;      // blocked vertically
    bool onGround = false;  // blocked while moving down (-y)
    bool onCeiling = false; // blocked while moving up (+y)
};

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
    bool isValid() const { return valid_; }

    // Sets every tile, or the tiles in a rectangle (clipped to the map).
    void fill(int32_t tileId);
    void fillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int32_t tileId);
    // Loads row-major tile ids, rows bottom to top. Returns false on a size
    // mismatch.
    bool setTiles(const std::vector<int32_t>& tiles);
    const std::vector<int32_t>& tiles() const { return tiles_; }

    // Tile containing a world point; false when outside the map.
    bool worldToTile(const Vector2& world, int32_t& x, int32_t& y) const;
    // Bottom-left corner of a tile in world space.
    Vector2 tileToWorld(int32_t x, int32_t y) const;
    Rect tileBounds(int32_t x, int32_t y) const;
    Rect worldBounds() const;

    // Solidity per tile id (ids >= 0). Unlisted non-empty tiles are solid.
    void setSolid(int32_t tileId, bool solid);
    bool isSolidTile(int32_t x, int32_t y) const; // false outside the map
    bool isSolidAt(const Vector2& world) const;
    // True if any solid tile overlaps the rect.
    bool overlapsSolid(const Rect& rect) const;

    // Moves box by delta, sliding along solid tiles: x is resolved first,
    // then y. Updates box in place. Keep per-step deltas below one tile to
    // avoid skipping thin walls.
    TileCollision moveAndSlide(Rect& box, const Vector2& delta) const;

    void draw(SpriteBatch& batch, const Camera2D& camera) const;

private:
    SpriteRegion tileRegion_(int32_t tileId) const;
    bool solidId_(int32_t tileId) const;

    TilemapDesc desc_;
    std::vector<int32_t> tiles_;
    std::vector<uint8_t> solidOverride_; // 0 = default, 1 = solid, 2 = passable
    bool valid_ = false;
};

} // namespace ion
