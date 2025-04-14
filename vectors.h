#ifndef VECTORS_H
#define VECTORS_H

#include <math.h>
#include <stdio.h>

typedef struct vec2 {
    float x;
    float y;
} vec2;

typedef struct vec3 {
    float x;
    float y;
    float z;
} vec3;

typedef struct vec4 {
    float x;
    float y;
    float z;
    float w;
} vec4;

// Matrix 4x4 type for transformations
typedef struct mat4 {
    float m[16];
} mat4;

// Vector operations
float length_vec2(vec2 a) {
    return sqrt(pow(a.x, 2) + pow(a.y, 2));
}

float length_vec3(vec3 a) {
    return sqrt(pow(a.x, 2) + pow(a.y, 2) + pow(a.z, 2));
}

float length_vec4(vec4 a) {
    return sqrt(pow(a.x, 2) + pow(a.y, 2) + pow(a.z, 2) + pow(a.w, 2));
}

void print_vec2(vec2 a) {
    printf("x: %f, y: %f\n", a.x, a.y);
}

void print_vec3(vec3 a) {
    printf("x: %f, y: %f, z: %f\n", a.x, a.y, a.z);
}

void print_vec4(vec4 a) {
    printf("x: %f, y: %f, z: %f, w: %f\n", a.x, a.y, a.z, a.w);
}

vec2 scale_vec2(vec2 a, float b) {
    vec2 result;
    result.x = a.x * b;
    result.y = a.y * b;
    return result;
}

vec3 scale_vec3(vec3 a, float b) {
    vec3 result;
    result.x = a.x * b;
    result.y = a.y * b;
    result.z = a.z * b;
    return result;
}

vec4 scale_vec4(vec4 a, float b) {
    vec4 result;
    result.x = a.x * b;
    result.y = a.y * b;
    result.z = a.z * b;
    result.w = a.w * b;
    return result;
}

vec2 add_vec2(vec2 a, vec2 b) {
    vec2 result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    return result;
}

vec3 add_vec3(vec3 a, vec3 b) {
    vec3 result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    result.z = a.z + b.z;
    return result;
}

vec4 add_vec4(vec4 a, vec4 b) {
    vec4 result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    result.z = a.z + b.z;
    result.w = a.w + b.w;
    return result;
}

vec2 subtract_vec2(vec2 a, vec2 b) {
    vec2 neg_b = scale_vec2(b, -1);
    return add_vec2(a, neg_b);
}

vec3 subtract_vec3(vec3 a, vec3 b) {
    vec3 neg_b = scale_vec3(b, -1);
    return add_vec3(a, neg_b);
}

vec4 subtract_vec4(vec4 a, vec4 b) {
    vec4 neg_b = scale_vec4(b, -1);
    return add_vec4(a, neg_b);
}

vec2 normalize_vec2(vec2 a) {
    float length = length_vec2(a);
    if (length != 0) {
        a.x /= length;
        a.y /= length;
    }
    return a;
}

vec3 normalize_vec3(vec3 a) {
    float length = length_vec3(a);
    if (length != 0) {
        a.x /= length;
        a.y /= length;
        a.z /= length;
    }
    return a;
}

vec4 normalize_vec4(vec4 a) {
    float length = length_vec4(a);
    if (length != 0) {
        a.x /= length;
        a.y /= length;
        a.z /= length;
        a.w /= length;
    }
    return a;
}

vec2 multiply_vec2(vec2 a, vec2 b) {
    a.x *= b.x;
    a.y *= b.y;
    return a;
}

vec3 multiply_vec3(vec3 a, vec3 b) {
    a.x *= b.x;
    a.y *= b.y;
    a.z *= b.z;
    return a;
}

vec4 multiply_vec4(vec4 a, vec4 b) {
    a.x *= b.x;
    a.y *= b.y;
    a.z *= b.z;
    a.w *= b.w;
    return a;
}

float dot_vec2(vec2 a, vec2 b) {
    return a.x * b.x + a.y * b.y;
}

float dot_vec3(vec3 a, vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float dot_vec4(vec4 a, vec4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// Cross product for vec3 (important for 3D operations)
vec3 cross_vec3(vec3 a, vec3 b) {
    vec3 result;
    result.x = a.y * b.z - a.z * b.y;
    result.y = a.z * b.x - a.x * b.z;
    result.z = a.x * b.y - a.y * b.x;
    return result;
}

vec2 rotate_vec2(vec2 v, float angle) {
    vec2 result;
    float cos_a = cos(angle);
    float sin_a = sin(angle);
    result.x = v.x * cos_a - v.y * sin_a;
    result.y = v.x * sin_a + v.y * cos_a;
    return result;
}

vec3 rotate_vec3_x(vec3 v, float angle) {
    vec3 result;
    float cos_a = cos(angle);
    float sin_a = sin(angle);
    result.x = v.x;
    result.y = v.y * cos_a - v.z * sin_a;
    result.z = v.y * sin_a + v.z * cos_a;
    return result;
}

vec3 rotate_vec3_y(vec3 v, float angle) {
    vec3 result;
    float cos_a = cos(angle);
    float sin_a = sin(angle);
    result.x = v.x * cos_a + v.z * sin_a;
    result.y = v.y;
    result.z = -v.x * sin_a + v.z * cos_a;
    return result;
}

vec3 rotate_vec3_z(vec3 v, float angle) {
    vec3 result;
    float cos_a = cos(angle);
    float sin_a = sin(angle);
    result.x = v.x * cos_a - v.y * sin_a;
    result.y = v.x * sin_a + v.y * cos_a;
    result.z = v.z;
    return result;
}

// Matrix operations
void print_mat4(mat4 m) {
    for (int i = 0; i < 4; i++) {
        printf("[%f, %f, %f, %f]\n", 
               m.m[i*4+0], m.m[i*4+1], m.m[i*4+2], m.m[i*4+3]);
    }
}

mat4 mat4_identity() {
    mat4 result;
    for (int i = 0; i < 16; i++) {
        result.m[i] = 0.0;
    }
    result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0;
    return result;
}

mat4 mat4_multiply(mat4 a, mat4 b) {
    mat4 result;
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i * 4 + j] = 
                a.m[i * 4 + 0] * b.m[0 * 4 + j] +
                a.m[i * 4 + 1] * b.m[1 * 4 + j] +
                a.m[i * 4 + 2] * b.m[2 * 4 + j] +
                a.m[i * 4 + 3] * b.m[3 * 4 + j];
        }
    }
    
    return result;
}

mat4 mat4_translate(mat4 m, vec3 v) {
    mat4 result = m;
    result.m[12] = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12];
    result.m[13] = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13];
    result.m[14] = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14];
    result.m[15] = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15];
    return result;
}

mat4 mat4_scale(mat4 m, vec3 v) {
    mat4 result = m;
    result.m[0] *= v.x;
    result.m[1] *= v.x;
    result.m[2] *= v.x;
    result.m[3] *= v.x;
    
    result.m[4] *= v.y;
    result.m[5] *= v.y;
    result.m[6] *= v.y;
    result.m[7] *= v.y;
    
    result.m[8] *= v.z;
    result.m[9] *= v.z;
    result.m[10] *= v.z;
    result.m[11] *= v.z;
    
    return result;
}

mat4 mat4_rotate_x(float angle) {
    mat4 result = mat4_identity();
    float c = cos(angle);
    float s = sin(angle);
    
    result.m[5] = c;
    result.m[6] = s;
    result.m[9] = -s;
    result.m[10] = c;
    
    return result;
}

mat4 mat4_rotate_y(float angle) {
    mat4 result = mat4_identity();
    float c = cos(angle);
    float s = sin(angle);
    
    result.m[0] = c;
    result.m[2] = -s;
    result.m[8] = s;
    result.m[10] = c;
    
    return result;
}

mat4 mat4_rotate_z(float angle) {
    mat4 result = mat4_identity();
    float c = cos(angle);
    float s = sin(angle);
    
    result.m[0] = c;
    result.m[1] = s;
    result.m[4] = -s;
    result.m[5] = c;
    
    return result;
}

mat4 mat4_perspective(float fovy, float aspect, float near_plane, float far_plane) {
    mat4 result = {0};
    float f = 1.0 / tan(fovy / 2.0);
    
    result.m[0] = f / aspect;
    result.m[5] = f;
    result.m[10] = (far_plane + near_plane) / (near_plane - far_plane);
    result.m[11] = -1.0;
    result.m[14] = (2.0 * far_plane * near_plane) / (near_plane - far_plane);
    
    return result;
}

mat4 mat4_ortho(float left, float right, float bottom, float top, float near_plane, float far_plane) {
    mat4 result = mat4_identity();
    
    result.m[0] = 2.0 / (right - left);
    result.m[5] = 2.0 / (top - bottom);
    result.m[10] = -2.0 / (far_plane - near_plane);
    
    result.m[12] = -(right + left) / (right - left);
    result.m[13] = -(top + bottom) / (top - bottom);
    result.m[14] = -(far_plane + near_plane) / (far_plane - near_plane);
    
    return result;
}

mat4 mat4_look_at(vec3 eye, vec3 center, vec3 up) {
    vec3 f = normalize_vec3(subtract_vec3(center, eye));
    vec3 s = normalize_vec3(cross_vec3(f, up));
    vec3 u = cross_vec3(s, f);
    
    mat4 result = mat4_identity();
    result.m[0] = s.x;
    result.m[4] = s.y;
    result.m[8] = s.z;
    
    result.m[1] = u.x;
    result.m[5] = u.y;
    result.m[9] = u.z;
    
    result.m[2] = -f.x;
    result.m[6] = -f.y;
    result.m[10] = -f.z;
    
    result.m[12] = -dot_vec3(s, eye);
    result.m[13] = -dot_vec3(u, eye);
    result.m[14] = dot_vec3(f, eye);
    
    return result;
}

// Transform a vec3 by a mat4
vec3 mat4_transform_vec3(mat4 m, vec3 v) {
    vec3 result;
    float w;
    
    result.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12];
    result.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13];
    result.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14];
    w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15];
    
    if (w != 0.0) {
        result.x /= w;
        result.y /= w;
        result.z /= w;
    }
    
    return result;
}

// Transform a vec4 by a mat4
vec4 mat4_transform_vec4(mat4 m, vec4 v) {
    vec4 result;
    
    result.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w;
    result.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w;
    result.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w;
    result.w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w;
    
    return result;
}

// Invert a mat4
mat4 mat4_invert(mat4 m) {
    mat4 result;
    float inv[16], det;
    int i;

    inv[0] = m.m[5]  * m.m[10] * m.m[15] - 
             m.m[5]  * m.m[11] * m.m[14] - 
             m.m[9]  * m.m[6]  * m.m[15] + 
             m.m[9]  * m.m[7]  * m.m[14] +
             m.m[13] * m.m[6]  * m.m[11] - 
             m.m[13] * m.m[7]  * m.m[10];

    inv[4] = -m.m[4]  * m.m[10] * m.m[15] + 
              m.m[4]  * m.m[11] * m.m[14] + 
              m.m[8]  * m.m[6]  * m.m[15] - 
              m.m[8]  * m.m[7]  * m.m[14] - 
              m.m[12] * m.m[6]  * m.m[11] + 
              m.m[12] * m.m[7]  * m.m[10];

    inv[8] = m.m[4]  * m.m[9] * m.m[15] - 
             m.m[4]  * m.m[11] * m.m[13] - 
             m.m[8]  * m.m[5] * m.m[15] + 
             m.m[8]  * m.m[7] * m.m[13] + 
             m.m[12] * m.m[5] * m.m[11] - 
             m.m[12] * m.m[7] * m.m[9];

    inv[12] = -m.m[4]  * m.m[9] * m.m[14] + 
               m.m[4]  * m.m[10] * m.m[13] +
               m.m[8]  * m.m[5] * m.m[14] - 
               m.m[8]  * m.m[6] * m.m[13] - 
               m.m[12] * m.m[5] * m.m[10] + 
               m.m[12] * m.m[6] * m.m[9];

    inv[1] = -m.m[1]  * m.m[10] * m.m[15] + 
              m.m[1]  * m.m[11] * m.m[14] + 
              m.m[9]  * m.m[2] * m.m[15] - 
              m.m[9]  * m.m[3] * m.m[14] - 
              m.m[13] * m.m[2] * m.m[11] + 
              m.m[13] * m.m[3] * m.m[10];

    inv[5] = m.m[0]  * m.m[10] * m.m[15] - 
             m.m[0]  * m.m[11] * m.m[14] - 
             m.m[8]  * m.m[2] * m.m[15] + 
             m.m[8]  * m.m[3] * m.m[14] + 
             m.m[12] * m.m[2] * m.m[11] - 
             m.m[12] * m.m[3] * m.m[10];

    inv[9] = -m.m[0]  * m.m[9] * m.m[15] + 
              m.m[0]  * m.m[11] * m.m[13] + 
              m.m[8]  * m.m[1] * m.m[15] - 
              m.m[8]  * m.m[3] * m.m[13] - 
              m.m[12] * m.m[1] * m.m[11] + 
              m.m[12] * m.m[3] * m.m[9];

    inv[13] = m.m[0]  * m.m[9] * m.m[14] - 
              m.m[0]  * m.m[10] * m.m[13] - 
              m.m[8]  * m.m[1] * m.m[14] + 
              m.m[8]  * m.m[2] * m.m[13] + 
              m.m[12] * m.m[1] * m.m[10] - 
              m.m[12] * m.m[2] * m.m[9];

    inv[2] = m.m[1]  * m.m[6] * m.m[15] - 
             m.m[1]  * m.m[7] * m.m[14] - 
             m.m[5]  * m.m[2] * m.m[15] + 
             m.m[5]  * m.m[3] * m.m[14] + 
             m.m[13] * m.m[2] * m.m[7] - 
             m.m[13] * m.m[3] * m.m[6];

    inv[6] = -m.m[0]  * m.m[6] * m.m[15] + 
              m.m[0]  * m.m[7] * m.m[14] + 
              m.m[4]  * m.m[2] * m.m[15] - 
              m.m[4]  * m.m[3] * m.m[14] - 
              m.m[12] * m.m[2] * m.m[7] + 
              m.m[12] * m.m[3] * m.m[6];

    inv[10] = m.m[0]  * m.m[5] * m.m[15] - 
              m.m[0]  * m.m[7] * m.m[13] - 
              m.m[4]  * m.m[1] * m.m[15] + 
              m.m[4]  * m.m[3] * m.m[13] + 
              m.m[12] * m.m[1] * m.m[7] - 
              m.m[12] * m.m[3] * m.m[5];

    inv[14] = -m.m[0]  * m.m[5] * m.m[14] + 
               m.m[0]  * m.m[6] * m.m[13] + 
               m.m[4]  * m.m[1] * m.m[14] - 
               m.m[4]  * m.m[2] * m.m[13] - 
               m.m[12] * m.m[1] * m.m[6] + 
               m.m[12] * m.m[2] * m.m[5];

    inv[3] = -m.m[1] * m.m[6] * m.m[11] + 
              m.m[1] * m.m[7] * m.m[10] + 
              m.m[5] * m.m[2] * m.m[11] - 
              m.m[5] * m.m[3] * m.m[10] - 
              m.m[9] * m.m[2] * m.m[7] + 
              m.m[9] * m.m[3] * m.m[6];

    inv[7] = m.m[0] * m.m[6] * m.m[11] - 
             m.m[0] * m.m[7] * m.m[10] - 
             m.m[4] * m.m[2] * m.m[11] + 
             m.m[4] * m.m[3] * m.m[10] + 
             m.m[8] * m.m[2] * m.m[7] - 
             m.m[8] * m.m[3] * m.m[6];

    inv[11] = -m.m[0] * m.m[5] * m.m[11] + 
               m.m[0] * m.m[7] * m.m[9] + 
               m.m[4] * m.m[1] * m.m[11] - 
               m.m[4] * m.m[3] * m.m[9] - 
               m.m[8] * m.m[1] * m.m[7] + 
               m.m[8] * m.m[3] * m.m[5];

    inv[15] = m.m[0] * m.m[5] * m.m[10] - 
              m.m[0] * m.m[6] * m.m[9] - 
              m.m[4] * m.m[1] * m.m[10] + 
              m.m[4] * m.m[2] * m.m[9] + 
              m.m[8] * m.m[1] * m.m[6] - 
              m.m[8] * m.m[2] * m.m[5];

    det = m.m[0] * inv[0] + m.m[1] * inv[4] + m.m[2] * inv[8] + m.m[3] * inv[12];

    if (det == 0)
        return m; // Return original matrix if inversion fails

    det = 1.0 / det;

    for (i = 0; i < 16; i++)
        result.m[i] = inv[i] * det;

    return result;
}

// Get transposed matrix
mat4 mat4_transpose(mat4 m) {
    mat4 result;
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i * 4 + j] = m.m[j * 4 + i];
        }
    }
    
    return result;
}

#endif // VECTORS_H

