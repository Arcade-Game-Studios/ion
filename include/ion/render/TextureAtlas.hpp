#pragma once

#include <ion/render/SpriteRegion.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace ion {

//
// A named collection of SpriteRegions over a single texture. Useful for
// sprite sheets (uniform grids) and hand-placed sprite atlases. Regions are
// pure data (no GPU resources are owned here), so instances can be cheaply
// copied and passed around.
//
class TextureAtlas {
public:
    TextureAtlas() = default;

    // Wraps an existing texture.
    void setTexture(const Texture& texture);

    // Adds a region at a pixel rect (x, y measured from the top-left).
    // Returns false if the name is already taken.
    bool addRegion(const std::string& name, uint32_t x, uint32_t y,
                   uint32_t width, uint32_t height);

    // Adds a uniform grid of cells spanning the whole texture, naming each
    // cell "<namePrefix>_<index>". Returns false if any name collides.
    bool addGrid(const std::string& namePrefix, uint32_t columns,
                 uint32_t rows);

    // Adds a uniform grid of cells with an explicit cell size (useful when
    // the texture has padding or the grid does not cover the whole texture).
    bool addGrid(const std::string& namePrefix, uint32_t columns,
                 uint32_t rows, uint32_t cellWidth, uint32_t cellHeight);

    const SpriteRegion* find(const std::string& name) const;
    const std::vector<std::string>& names() const;
    const Texture& texture() const;
    size_t regionCount() const;

private:
    Texture texture_;
    std::unordered_map<std::string, SpriteRegion> regions_;
    std::vector<std::string> order_;
};

} // namespace ion
