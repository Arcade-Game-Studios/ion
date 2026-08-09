#pragma once

#include <ion/math/Vector3.hpp>

#include <cstring>

namespace ion {

struct Matrix4;

inline Matrix4 operator*(const Matrix4& a, const Matrix4& b);

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
                                float near, float far) {
        Matrix4 mat = identity();
        mat.m[0] = 2.0f / (right - left);
        mat.m[5] = 2.0f / (top - bottom);
        mat.m[10] = -2.0f / (far - near);
        mat.m[12] = -(right + left) / (right - left);
        mat.m[13] = -(top + bottom) / (top - bottom);
        mat.m[14] = -(far + near) / (far - near);
        return mat;
    }

    static Matrix4 perspective(float fovRadians, float aspect,
                               float near, float far) {
        Matrix4 mat;
        float f = 1.0f / std::tan(fovRadians * 0.5f);
        mat.m[0] = f / aspect;
        mat.m[5] = f;
        mat.m[10] = (far + near) / (near - far);
        mat.m[11] = -1.0f;
        mat.m[14] = (2.0f * far * near) / (near - far);
        return mat;
    }

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

inline Vector3 operator*(const Matrix4& mat, const Vector3& vec) {
    float x = mat.m[0] * vec.x + mat.m[4] * vec.y + mat.m[8] * vec.z + mat.m[12];
    float y = mat.m[1] * vec.x + mat.m[5] * vec.y + mat.m[9] * vec.z + mat.m[13];
    float z = mat.m[2] * vec.x + mat.m[6] * vec.y + mat.m[10] * vec.z + mat.m[14];
    return Vector3(x, y, z);
}

} // namespace ion
