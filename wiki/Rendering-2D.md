# 2D Rendering

2D rendering types live in `ion::` and are declared under
`include/ion/render/`. Everything is built on `SpriteBatch`, which batches
quads and flushes them grouped by texture.

## SpriteRegion

A pixel-space rectangle inside a texture, with precomputed UVs.

```cpp
ion::SpriteRegion full = ion::SpriteRegion::full(texture);
full.x = 16; full.y = 0; full.width = 16; full.height = 16;
full.computeUV();
// full.u0/v0/u1/v1 are now valid
```

`region.isValid()` is true when the texture and size are valid.

## Camera2D

World coordinates are in pixels with **+y up**. `position()` is the world point
at the center of the screen; `zoom()` scales world units per pixel (1.0 = one
unit per pixel).

```cpp
ion::Camera2D camera;
camera.setViewport(window.width(), window.height());
camera.setPosition(ion::Vector2(0.0f, 0.0f));
camera.setZoom(3.0f);
camera.setRotation(0.0f);

ion::Vector2 world = camera.screenToWorld(mouseX, mouseY);
ion::Vector2 screen = camera.worldToScreen(someWorldPoint);
ion::Matrix4 vp = camera.viewProjection(); // used internally by the batch
```

Call `setViewport()` whenever the window is resized to keep the projection
in sync. The `Camera2D` stores viewport dimensions and builds an orthographic
projection from them — if the viewport is stale after a resize, sprites will
stretch.

```cpp
// In your onResize callback or update loop:
camera.setViewport(window.width(), window.height());
```

## SpriteBatch

The core batching API. Everything drawn with the batch shares its built-in
shader (position/color/uv, an orthographic `uMVP`, and one texture at slot 0).
Untextured primitives use a built-in 1x1 white texture tinted by vertex color.

```cpp
ion::SpriteBatch batch;
batch.initialize(&renderer, /*maxQuads=*/8192);

// In render():
renderer.beginFrame();
renderer.clear(ion::Color(0.12f, 0.14f, 0.20f));

batch.begin(worldCamera);
batch.drawSprite(region, ion::Vector2(100.0f, 100.0f), ion::Vector2(64, 64));
batch.drawRect(ion::Vector2(10, 10), ion::Vector2(100, 100), ion::Color::red());
batch.drawLine(ion::Vector2(0, 0), ion::Vector2(200, 200), 2.0f, ion::Color::yellow());
batch.drawCircleOutline(center, 40.0f, 2.0f, ion::Color::cyan());
batch.end();

renderer.endFrame();
```

Draw calls:

- `drawSprite(region, position, size, rotation = 0, origin = center, color)`
  — scaled quad; `origin` is the point inside the sprite placed at `position`
  (defaults to the center), rotation is in radians about the origin.
- `drawSprite(region, position, scale = 1, rotation = 0, color)`
  — native pixel size scaled by `scale`.
- `drawRect(pos, size, color)`, `drawRectOutline(pos, size, thickness, color)`
- `drawLine(from, to, thickness, color)`
- `drawCircle(center, radius, color, segments = 24)`
- `drawCircleOutline(center, radius, thickness, color, segments = 24)`

Introspection: `drawCallCount()` (GPU draws this frame) and `quadCount()`
(quads pending in the current batch).

Use multiple passes with different cameras for world vs screen-space UI:

```cpp
batch.begin(worldCamera);
// ... world content ...
batch.end();

batch.begin(uiCamera); // 1 world unit = 1 pixel
// ... screen-space HUD ...
batch.end();
```

## TextureAtlas

A named collection of `SpriteRegion`s over one texture.

```cpp
ion::TextureAtlas atlas;
atlas.setTexture(spriteSheet);

atlas.addRegion("hero_0", 0, 48, 16, 16);            // pixel rect
atlas.addGrid("tile", 4, 4);                          // whole-texture grid
atlas.addGrid("icons", 4, 2, 16, 16);                 // explicit cell size

const ion::SpriteRegion* hero = atlas.find("hero_0");
atlas.regionCount();   // number of regions
atlas.names();         // insertion-ordered names
atlas.texture();       // the wrapped texture
```

`addRegion` / `addGrid` return `false` on a name collision.

## SpriteAnimation

Frame-based animation over `SpriteRegion`s.

```cpp
ion::SpriteAnimation anim;
anim.setFrames({*atlas.find("hero_0"), *atlas.find("hero_1"),
                *atlas.find("hero_2")}, /*frameDuration=*/0.18f);
anim.play(true);      // loop by default; stop()/pause()/resume() also exist

// Each frame:
anim.update(deltaTime);
batch.drawSprite(anim.currentFrame(), position, ion::Vector2(64, 64));
```

State: `frameIndex()`, `isPlaying()`, `isFinished()`, `duration()`, `frameCount()`.

## Tilemap

A grid of tiles referencing cells of a tileset texture. `draw()` culls to the
camera's visible range, so cost scales with screen area, not map size.

```cpp
ion::TilemapDesc desc;
desc.width = 48;
desc.height = 48;
desc.tileSize = 16;
desc.tileset = spriteSheet;
desc.tilesAcross = 4; // columns of 16x16 tiles in the sheet

ion::Tilemap map;
map.create(desc);
map.setTile(5, 5, 0);   // tile index into the tileset grid
map.setTile(6, 5, -1);  // -1 = empty

// Each frame, between batch.begin() and batch.end():
map.draw(batch, worldCamera);
```

API: `create(desc)`, `destroy()`, `setTile(x, y, id)`, `tile(x, y)`,
`empty(x, y)`, `width()`, `height()`, `tileSize()`, `draw(batch, camera)`.

## ParticleSystem

A fixed-capacity 2D particle emitter supporting continuous emission and bursts.
Particles are tinted quads; `draw()` must be called between `batch.begin()` and
`batch.end()`.

```cpp
ion::ParticleEmitterConfig cfg;
cfg.region = glow;            // optional; invalid region = tinted white quad
cfg.capacity = 512;
cfg.rate = 40.0f;             // particles/second; 0 = burst-only
cfg.lifetime = 0.6f;
cfg.lifetimeSpread = 0.2f;
cfg.speed = 36.0f;
cfg.speedSpread = 14.0f;
cfg.angle = -1.5707963f;      // radians; 0 = +x, pi/2 = +y
cfg.angleSpread = 0.9f;
cfg.gravity = ion::Vector2(0.0f, -50.0f);
cfg.startSize = 7.0f;
cfg.endSize = 2.0f;
cfg.startColor = ion::Color(1.0f, 0.85f, 0.3f);
cfg.endColor = ion::Color::transparent();

ion::ParticleEmitter emitter;
emitter.setConfig(cfg);
emitter.setPosition(player);
emitter.start();              // or emit a one-shot burst:
emitter.burst(24);

// Each frame:
emitter.update(deltaTime);
emitter.draw(batch);
```

API: `setConfig`, `setPosition`, `burst(count)`, `start`, `stop`, `isActive`,
`clear`, `update`, `draw`, `aliveCount`.

## Text

Text rendering with two modes: a built-in 5x7 bitmap font and TrueType font
loading via stb_truetype.

### Built-in bitmap font

No external files needed. `initialize()` builds a glyph atlas on the GPU
from a hardcoded 5x7 pixel font (ASCII 32–126).

```cpp
ion::Font font;
font.initialize(&renderer);

// Between batch.begin() and batch.end(), glyphs scaled to 16 px tall:
font.draw(batch, "Hello, world!", ion::Vector2(12.0f, -12.0f), 16.0f,
          ion::Color::white());

ion::Vector2 size = font.measure("Hello, world!", 16.0f);
```

### TrueType fonts

Load any `.ttf` file for proportional, anti-aliased text at any size.

```cpp
ion::Font font;
font.loadFromFile(&renderer, "assets/arial.ttf", 24.0f);

// Use exactly like the bitmap font — glyphHeight is ignored for TTF fonts
// (the size from loadFromFile is used):
font.draw(batch, "Score: 100", ion::Vector2(10.0f, -10.0f), 0.0f,
          ion::Color::white());

ion::Vector2 size = font.measure("Score: 100", 0.0f);
```

- `loadFromFile(renderer, path, fontSize)` — `fontSize` is the pixel height of
  uppercase letters. Returns `false` if the file can't be loaded or parsed.
- Proportional spacing — each character advances by its natural width.
- Smooth anti-aliased rendering via stb_truetype.
- `isTrueType()` returns `true` after a successful TTF load.

Both modes share the same `draw()` and `measure()` API. `'\n'` inserts a
line break. `font.texture()`, `glyphWidth()`, and `glyphHeight()` are also
exposed.

## Example

`examples/2d` demonstrates all of the above: a procedural sprite sheet, a tiled
map with a lake and sand patch, an animated hero with WASD movement, sway,
glow particles with a Space-bar burst, and a screen-space font HUD.
