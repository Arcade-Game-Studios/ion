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
