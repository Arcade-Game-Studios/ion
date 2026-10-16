#pragma once

#include <ion/math/Vector3.hpp>
#include <ion/math/Vector4.hpp>

#include <cmath>
#include <cstring>

namespace ion {

struct Matrix4;

inline Matrix4 operator*(const Matrix4& a, const Matrix4& b);

//
// 4x4 float matrix stored column-major (m[col * 4 + row]), matching OpenGL,
// Metal and glTF conventions. Vectors are columns: v' = M * v, so in
// A * B the transform B is applied first.
//
// Parameter names avoid `near`/`far`, which <windows.h> defines as macros.
//
struct Matrix4 {
    float m[16];

    Matrix4() {
        memset(m, 0, sizeof(m));
    }

    static Matrix4 identity() {
        Matrix4 mat;
        mat.m[0] = 1.0f;
        mat.m[5] = 1.0f;
        mat.m[10] = 1.0f;
        mat.m[15] = 1.0f;
        return mat;
    }

    static Matrix4 translation(const Vector3& t) {
        Matrix4 mat = identity();
        mat.m[12] = t.x;
        mat.m[13] = t.y;
        mat.m[14] = t.z;
        return mat;
    }

    static Matrix4 scale(const Vector3& s) {
        Matrix4 mat = identity();
        mat.m[0] = s.x;
        mat.m[5] = s.y;
        mat.m[10] = s.z;
        return mat;
    }

    static Matrix4 rotationX(float angle) {
        Matrix4 mat = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        mat.m[5] = c;
        mat.m[9] = -s;
        mat.m[6] = s;
        mat.m[10] = c;
        return mat;
    }

    static Matrix4 rotationY(float angle) {
        Matrix4 mat = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        mat.m[0] = c;
        mat.m[8] = s;
        mat.m[2] = -s;
        mat.m[10] = c;
        return mat;
    }

    // Right-handed rotation of `angle` radians around `axis` (normalized
    // internally). Returns identity for a zero axis.
    static Matrix4 rotation(const Vector3& axis, float angle) {
        Vector3 a = axis.normalized();
        if (a.isZero()) {
            return identity();
        }
        float c = std::cos(angle);
        float s = std::sin(angle);
        float t = 1.0f - c;
        Matrix4 mat = identity();
        mat(0, 0) = t * a.x * a.x + c;
        mat(0, 1) = t * a.x * a.y - s * a.z;
        mat(0, 2) = t * a.x * a.z + s * a.y;
        mat(1, 0) = t * a.x * a.y + s * a.z;
        mat(1, 1) = t * a.y * a.y + c;
        mat(1, 2) = t * a.y * a.z - s * a.x;
        mat(2, 0) = t * a.x * a.z - s * a.y;
        mat(2, 1) = t * a.y * a.z + s * a.x;
        mat(2, 2) = t * a.z * a.z + c;
        return mat;
    }

    static Matrix4 rotationZ(float angle) {
        Matrix4 mat = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        mat.m[0] = c;
        mat.m[4] = -s;
        mat.m[1] = s;
        mat.m[5] = c;
        return mat;
    }

    static Matrix4 orthographic(float left, float right,
                                float bottom, float top,
                                float zNear, float zFar) {
        Matrix4 mat = identity();
        mat.m[0] = 2.0f / (right - left);
        mat.m[5] = 2.0f / (top - bottom);
        mat.m[10] = -2.0f / (zFar - zNear);
        mat.m[12] = -(right + left) / (right - left);
        mat.m[13] = -(top + bottom) / (top - bottom);
        mat.m[14] = -(zFar + zNear) / (zFar - zNear);
        return mat;
    }

    static Matrix4 perspective(float fovRadians, float aspect,
                               float zNear, float zFar) {
        Matrix4 mat;
        float f = 1.0f / std::tan(fovRadians * 0.5f);
        mat.m[0] = f / aspect;
        mat.m[5] = f;
        mat.m[10] = (zFar + zNear) / (zNear - zFar);
        mat.m[11] = -1.0f;
        mat.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
        return mat;
    }

    // Translation * rotation (Euler, applied Z, then X, then Y) * scale.
    static Matrix4 compose(const Vector3& translationPart,
                           const Vector3& eulerRadians,
                           const Vector3& scalePart);

    static Matrix4 lookAt(const Vector3& eye, const Vector3& target,
                          const Vector3& up) {
        Vector3 f = (target - eye).normalized();
        Vector3 s = Vector3::cross(f, up).normalized();
        Vector3 u = Vector3::cross(s, f);

        Matrix4 mat = identity();
        mat.m[0] = s.x;
        mat.m[4] = s.y;
        mat.m[8] = s.z;
        mat.m[1] = u.x;
        mat.m[5] = u.y;
        mat.m[9] = u.z;
        mat.m[2] = -f.x;
        mat.m[6] = -f.y;
        mat.m[10] = -f.z;
        mat.m[12] = -Vector3::dot(s, eye);
        mat.m[13] = -Vector3::dot(u, eye);
        mat.m[14] = Vector3::dot(f, eye);
        return mat;
    }

    // General 4x4 inverse in the same column-major layout. Returns the zero
    // matrix if the matrix is not invertible.
    static Matrix4 inverse(const Matrix4& mat) {
        const float* m = mat.m;
        Matrix4 inv;
        float* o = inv.m;
        o[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] -
               m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
               m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        o[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] +
               m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
               m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        o[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] -
               m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
               m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        o[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] +
                m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
                m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        o[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] +
               m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
               m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        o[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] -
               m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
               m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        o[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] +
               m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
               m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        o[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] -
                m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
                m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        o[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] -
               m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
               m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        o[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] +
               m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
               m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        o[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] -
                m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
                m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        o[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] +
                m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
                m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        o[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] +
               m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
               m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        o[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] -
               m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
               m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        o[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] +
                m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
                m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        o[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] -
                m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
                m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

        float det = m[0] * o[0] + m[1] * o[4] + m[2] * o[8] + m[3] * o[12];
        if (det == 0.0f) {
            return Matrix4();
        }
        float invDet = 1.0f / det;
        for (int i = 0; i < 16; i++) {
            o[i] *= invDet;
        }
        return inv;
    }

    static Matrix4 transpose(const Matrix4& mat) {
        Matrix4 out;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                out(row, col) = mat(col, row);
            }
        }
        return out;
    }

    Matrix4 transposed() const { return transpose(*this); }
    Matrix4 inverted() const { return inverse(*this); }

    float determinant() const {
        const float* a = m;
        float c0 = a[5] * (a[10] * a[15] - a[14] * a[11]) -
                   a[9] * (a[6] * a[15] - a[14] * a[7]) +
                   a[13] * (a[6] * a[11] - a[10] * a[7]);
        float c1 = a[1] * (a[10] * a[15] - a[14] * a[11]) -
                   a[9] * (a[2] * a[15] - a[14] * a[3]) +
                   a[13] * (a[2] * a[11] - a[10] * a[3]);
        float c2 = a[1] * (a[6] * a[15] - a[14] * a[7]) -
                   a[5] * (a[2] * a[15] - a[14] * a[3]) +
                   a[13] * (a[2] * a[7] - a[6] * a[3]);
        float c3 = a[1] * (a[6] * a[11] - a[10] * a[7]) -
                   a[5] * (a[2] * a[11] - a[10] * a[3]) +
                   a[9] * (a[2] * a[7] - a[6] * a[3]);
        return a[0] * c0 - a[4] * c1 + a[8] * c2 - a[12] * c3;
    }

    // Translation column.
    Vector3 getTranslation() const { return Vector3(m[12], m[13], m[14]); }

    // Per-axis scale (lengths of the basis columns).
    Vector3 getScale() const {
        return Vector3(Vector3(m[0], m[1], m[2]).length(),
                       Vector3(m[4], m[5], m[6]).length(),
                       Vector3(m[8], m[9], m[10]).length());
    }

    // Transforms a point (w = 1) and applies the perspective divide.
    Vector3 transformPoint(const Vector3& p) const {
        float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
        float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
        float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        if (w != 0.0f && w != 1.0f) {
            return Vector3(x / w, y / w, z / w);
        }
        return Vector3(x, y, z);
    }

    // Transforms a direction (w = 0): ignores translation.
    Vector3 transformDirection(const Vector3& d) const {
        return Vector3(m[0] * d.x + m[4] * d.y + m[8] * d.z,
                       m[1] * d.x + m[5] * d.y + m[9] * d.z,
                       m[2] * d.x + m[6] * d.y + m[10] * d.z);
    }

    float& operator()(int row, int col) {
        return m[col * 4 + row];
    }

    float operator()(int row, int col) const {
        return m[col * 4 + row];
    }

    Matrix4& operator*=(const Matrix4& other) {
        *this = *this * other;
        return *this;
    }
};

inline Matrix4 operator*(const Matrix4& a, const Matrix4& b) {
    Matrix4 result;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a(row, k) * b(k, col);
            }
            result(row, col) = sum;
        }
    }
    return result;
}

inline Vector4 operator*(const Matrix4& mat, const Vector4& v) {
    return Vector4(mat.m[0] * v.x + mat.m[4] * v.y + mat.m[8] * v.z + mat.m[12] * v.w,
                   mat.m[1] * v.x + mat.m[5] * v.y + mat.m[9] * v.z + mat.m[13] * v.w,
                   mat.m[2] * v.x + mat.m[6] * v.y + mat.m[10] * v.z + mat.m[14] * v.w,
                   mat.m[3] * v.x + mat.m[7] * v.y + mat.m[11] * v.z + mat.m[15] * v.w);
}

inline bool operator==(const Matrix4& a, const Matrix4& b) {
    return std::memcmp(a.m, b.m, sizeof(a.m)) == 0;
}

inline bool operator!=(const Matrix4& a, const Matrix4& b) {
    return !(a == b);
}

inline bool approxEqual(const Matrix4& a, const Matrix4& b,
                        float epsilon = 1e-5f) {
    for (int i = 0; i < 16; i++) {
        if (std::fabs(a.m[i] - b.m[i]) > epsilon) {
            return false;
        }
    }
    return true;
}

inline Matrix4 Matrix4::compose(const Vector3& translationPart,
                                const Vector3& eulerRadians,
                                const Vector3& scalePart) {
    return translation(translationPart) * rotationY(eulerRadians.y) *
           rotationX(eulerRadians.x) * rotationZ(eulerRadians.z) *
           scale(scalePart);
}

// Affine transform of a point; ignores the projective row (use
// Matrix4::transformPoint for projection matrices).
inline Vector3 operator*(const Matrix4& mat, const Vector3& vec) {
    float x = mat.m[0] * vec.x + mat.m[4] * vec.y + mat.m[8] * vec.z + mat.m[12];
    float y = mat.m[1] * vec.x + mat.m[5] * vec.y + mat.m[9] * vec.z + mat.m[13];
    float z = mat.m[2] * vec.x + mat.m[6] * vec.y + mat.m[10] * vec.z + mat.m[14];
    return Vector3(x, y, z);
}

} // namespace ion
