# Math

Math types live in `ion::` and are declared under `include/ion/math/`.

## Vector2

```cpp
ion::Vector2 v(3.0f, 4.0f);
ion::Vector2 w = v + ion::Vector2(1, 1);
ion::Vector2 u = v.normalized();
float len = v.length();
float len2 = v.lengthSquared();
float d = ion::Vector2::dot(a, b);
```

Operators: `+`, `-`, `*` (scalar, both sides), `/` (scalar), `==`, `!=`,
`+=`, `-=`, `*=`, `/=`. Default-constructed `Vector2` is `(0, 0)`.

## Vector3 / Vector4

`Vector3(x, y, z)` and `Vector4(x, y, z, w)` mirror `Vector2`'s operators and
add cross product for 3D:

```cpp
ion::Vector3 n = ion::Vector3::cross(a, b);
float d = ion::Vector3::dot(a, b);
```

## Matrix4

Column-major 4x4 matrix (`m[col * 4 + row]`). Index with
`mat(row, col)` or index the raw `m[16]` array.

Factories:

- `Matrix4::identity()`
- `Matrix4::translation(Vector3)`
- `Matrix4::scale(Vector3)`
- `Matrix4::rotationZ(radians)`
- `Matrix4::orthographic(left, right, bottom, top, near, far)`
- `Matrix4::perspective(fovRadians, aspect, near, far)`
- `Matrix4::lookAt(eye, target, up)`

Operators: `*` (matrix multiply, and `Matrix4 * Vector3`),
`*=`, and `operator()(row, col)` accessors.

Example:

```cpp
ion::Matrix4 model =
    ion::Matrix4::rotationZ(angle) *
    ion::Matrix4::translation(ion::Vector3(1.2f, 0.0f, 0.0f));
ion::Matrix4 mvp = projection * (view * model);
renderer.setUniform("uMVP", mvp);
```

More `Matrix4` helpers: `rotationX/Y`, `rotation(axis, radians)`,
`compose(translation, eulerRadians, scale)`, `transpose`, `inverse` /
`inverted()`, `determinant()`, `transformPoint` (with perspective divide),
`transformDirection`, `getTranslation`, `getScale`, `Matrix4 * Vector4`,
`==` and `approxEqual`.

## Vector extras

All vectors support unary `-`, component-wise `*` and `/`, `[]` indexing,
`lerp`, `min`/`max`/`abs`, `distance`, `reflect`, `project`,
`angleBetween`, `moveTowards`, `clampedLength` and `approxEqual`.
`Vector2` adds `angle()`, `rotated()`, `perpendicular()`, `fromAngle()` and a
2D `cross`; `Vector3` adds `projectOnPlane` and constants (`up()`,
`forward()` = -Z, `right()`).

## Scalar helpers — `MathUtils.hpp`

`PI`, `TAU`, `radians()`, `degrees()`, `clamp`, `saturate`, `lerp`,
`inverseLerp`, `remap`, `smoothstep`, `smootherstep`, `moveTowards`,
`wrapAngle`, `angleDifference`, `lerpAngle`, `damp` (frame-rate independent
smoothing), `repeat`, `pingPong`, `nextPowerOfTwo`.

## Quaternion

```cpp
ion::Quaternion q = ion::Quaternion::fromAxisAngle(ion::Vector3::up(), ion::radians(90));
ion::Vector3 v = q * ion::Vector3(0, 0, 1);              // rotate
ion::Quaternion mid = ion::Quaternion::slerp(a, b, 0.5f);
ion::Matrix4 model = ion::composeTransform(position, q, scale);
```

Also `fromEuler`/`toEuler`, `fromTo`, `lookRotation`, `fromMatrix`/
`toMatrix`, `nlerp`, `inverse`, `angleBetween`. `(a * b)` applies `b` first,
like matrices.

## Geometry and collision — `Geometry.hpp`

2D: `Rect` (contains, intersects, intersection, merged, expanded,
closestPoint), `Circle`, `Segment2D`, `Ray2D`.

```cpp
ion::Manifold2D m = ion::collide(playerRect, enemyCircle);
if (m.colliding) player.position -= m.normal * m.depth;   // push out
ion::RayHit2D hit = ion::raycast(ion::Ray2D{eye, dir}, wall);
float toi; ion::Vector2 n;
if (ion::sweep(box, velocity * dt, wall, toi, n)) { ... } // no tunnelling
```

Plus segment intersection, `closestPointOnSegment` and `pointInPolygon`.

3D: `AABB` (with `transformed(matrix)`), `Sphere`, `Plane`, `Ray`;
raycasts against boxes, spheres, planes and triangles; and `Frustum`
culling:

```cpp
ion::Frustum frustum = ion::Frustum::fromMatrix(camera.viewProjection());
if (frustum.intersects(model.bounds())) draw(model);
```

## Random

`ion::Random` is a seedable PCG32 generator, deterministic across platforms
(unlike `std::uniform_*_distribution`), for replays and procedural content:
`range(int, int)`, `range(float, float)`, `chance(p)`, `gaussian`,
`unitVector2/3`, `insideUnitCircle/Sphere`, `pick`, `weightedIndex`,
`shuffle`.

## Color

`ion::Color` (in `ion/render/Color.hpp`): `fromHex(0xRRGGBB)`,
`fromHexRGBA`, `fromHSV`/`toHSV`, `toRGBA8`, `lerp`, `withAlpha`,
`premultiplied`, `luminance`, `lighten`, operators `*` (tint), `+`, `-`,
and more named colors.
