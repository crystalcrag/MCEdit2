/*
 * Utils.h : classical datatypes and helper functions for opengl
 */


#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

typedef uint16_t *               DATA16;

typedef float                    vec4[4];
typedef float                    mat4[16];

enum /* stored column first like GLSL */
{
	A00 = 0,
	A10 = 1,
	A20 = 2,
	A30 = 3,
	A01 = 4,
	A11 = 5,
	A21 = 6,
	A31 = 7,
	A02 = 8,
	A12 = 9,
	A22 = 10,
	A32 = 11,
	A03 = 12,
	A13 = 13,
	A23 = 14,
	A33 = 15
};

enum
{
	VX = 0,
	VY = 1,
	VZ = 2,
	VT = 3
};

/* res can point to A or B */
void matTranspose(mat4 A);
void matAdd(mat4 res, mat4 A, mat4 B);
void matMult(mat4 res, mat4 A, mat4 B);
void matMultByVec(vec4 res, mat4 A, vec4 B);
void matMultByVec3(vec4 res, mat4 A, vec4 B);
void matInverseTranspose(mat4 res, mat4 A);

/* generate a transformation matrix in res */
void matTranslate(mat4 res, float x, float y, float z);
void matScale(mat4 res, float x, float y, float z);
void matRotate(mat4 res, float theta, int axis_0X_1Y_2Z);
void matIdent(mat4 res);

/* perspective matrix */
void matPerspective(mat4 res, float fov_deg, float aspect, float znear, float zfar);
void matOrtho(mat4 res, float left, float right, float top, float bottom, float znear, float zfar);
void matLookAt(mat4 res, float eyeX,  float eyeY,  float eyeZ, float centerX, float centerY, float centerZ, float upX, float upY, float upZ);

void matPrint(mat4 A);

/* vector operation (res can point to A or B) */
float vecLength(vec4 A);
void  vecNormalize(vec4 res, vec4 A);
float vecDotProduct(vec4 A, vec4 B);
void  vecCrossProduct(vec4 res, vec4 A, vec4 B);
void  vecSub(vec4 res, vec4 A, vec4 B);
void  vecAdd(vec4 res, vec4 A, vec4 B);

#endif
