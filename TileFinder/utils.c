/*
 * Utils.c: utility function to deal with opengl and 3d math
 */


#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include "Utils.h"

/*
 * classical matrix related operations
 */
void matTranspose(mat4 A)
{
	float tmp;

	tmp = A[A10]; A[A10] = A[A01]; A[A01] = tmp;
	tmp = A[A20]; A[A20] = A[A02]; A[A02] = tmp;
	tmp = A[A30]; A[A30] = A[A03]; A[A03] = tmp;
	tmp = A[A12]; A[A12] = A[A21]; A[A21] = tmp;
	tmp = A[A13]; A[A13] = A[A31]; A[A31] = tmp;
	tmp = A[A23]; A[A23] = A[A32]; A[A32] = tmp;
}

void matAdd(mat4 res, mat4 A, mat4 B)
{
	int i;
	for (i = 0; i < 16; i ++)
		res[i] = A[i] + B[i];
}

void matMult(mat4 res, mat4 A, mat4 B)
{
	mat4 tmp;

	tmp[A00] = A[A00]*B[A00] + A[A01]*B[A10] + A[A02]*B[A20] + A[A03]*B[A30];
	tmp[A10] = A[A10]*B[A00] + A[A11]*B[A10] + A[A12]*B[A20] + A[A13]*B[A30];
	tmp[A20] = A[A20]*B[A00] + A[A21]*B[A10] + A[A22]*B[A20] + A[A23]*B[A30];
	tmp[A30] = A[A30]*B[A00] + A[A31]*B[A10] + A[A32]*B[A20] + A[A33]*B[A30];
	tmp[A01] = A[A00]*B[A01] + A[A01]*B[A11] + A[A02]*B[A21] + A[A03]*B[A31];
	tmp[A11] = A[A10]*B[A01] + A[A11]*B[A11] + A[A12]*B[A21] + A[A13]*B[A31];
	tmp[A21] = A[A20]*B[A01] + A[A21]*B[A11] + A[A22]*B[A21] + A[A23]*B[A31];
	tmp[A31] = A[A30]*B[A01] + A[A31]*B[A11] + A[A32]*B[A21] + A[A33]*B[A31];
	tmp[A02] = A[A00]*B[A02] + A[A01]*B[A12] + A[A02]*B[A22] + A[A03]*B[A32];
	tmp[A12] = A[A10]*B[A02] + A[A11]*B[A12] + A[A12]*B[A22] + A[A13]*B[A32];
	tmp[A22] = A[A20]*B[A02] + A[A21]*B[A12] + A[A22]*B[A22] + A[A23]*B[A32];
	tmp[A32] = A[A30]*B[A02] + A[A31]*B[A12] + A[A32]*B[A22] + A[A33]*B[A32];
	tmp[A03] = A[A00]*B[A03] + A[A01]*B[A13] + A[A02]*B[A23] + A[A03]*B[A33];
	tmp[A13] = A[A10]*B[A03] + A[A11]*B[A13] + A[A12]*B[A23] + A[A13]*B[A33];
	tmp[A23] = A[A20]*B[A03] + A[A21]*B[A13] + A[A22]*B[A23] + A[A23]*B[A33];
	tmp[A33] = A[A30]*B[A03] + A[A31]*B[A13] + A[A32]*B[A23] + A[A33]*B[A33];

	memcpy(res, tmp, sizeof tmp);
}

void matMultByVec(vec4 res, mat4 A, vec4 B)
{
	vec4 tmp;

	tmp[VX] = A[A00]*B[VX] + A[A01]*B[VY] + A[A02]*B[VZ] + A[A03]*B[VT];
	tmp[VY] = A[A10]*B[VX] + A[A11]*B[VY] + A[A12]*B[VZ] + A[A13]*B[VT];
	tmp[VZ] = A[A20]*B[VX] + A[A21]*B[VY] + A[A22]*B[VZ] + A[A23]*B[VT];
	tmp[VT] = A[A30]*B[VX] + A[A31]*B[VY] + A[A32]*B[VZ] + A[A33]*B[VT];

	memcpy(res, tmp, sizeof tmp);
}

void matMultByVec3(vec4 res, mat4 A, vec4 B)
{
	vec4 tmp;

	tmp[VX] = A[A00]*B[VX] + A[A01]*B[VY] + A[A02]*B[VZ] + A[A03];
	tmp[VY] = A[A10]*B[VX] + A[A11]*B[VY] + A[A12]*B[VZ] + A[A13];
	tmp[VZ] = A[A20]*B[VX] + A[A21]*B[VY] + A[A22]*B[VZ] + A[A23];

	memcpy(res, tmp, 3 * sizeof (float));
}

/*
 * taken from glm library: convert a matrix intended for vertex so that it can be applied to a vector
 * (has to ignore translation). Not: normalization will still be required if used on a normal.
 */
void matInverseTranspose(mat4 res, mat4 m)
{
	float SubFactor00 = m[A22] * m[A33] - m[A32] * m[A23];
	float SubFactor01 = m[A21] * m[A33] - m[A31] * m[A23];
	float SubFactor02 = m[A21] * m[A32] - m[A31] * m[A22];
	float SubFactor03 = m[A20] * m[A33] - m[A30] * m[A23];
	float SubFactor04 = m[A20] * m[A32] - m[A30] * m[A22];
	float SubFactor05 = m[A20] * m[A31] - m[A30] * m[A21];
	float SubFactor06 = m[A12] * m[A33] - m[A32] * m[A13];
	float SubFactor07 = m[A11] * m[A33] - m[A31] * m[A13];
	float SubFactor08 = m[A11] * m[A32] - m[A31] * m[A12];
	float SubFactor09 = m[A10] * m[A33] - m[A30] * m[A13];
	float SubFactor10 = m[A10] * m[A32] - m[A30] * m[A12];
	float SubFactor11 = m[A10] * m[A31] - m[A30] * m[A11];
	float SubFactor12 = m[A12] * m[A23] - m[A22] * m[A13];
	float SubFactor13 = m[A11] * m[A23] - m[A21] * m[A13];
	float SubFactor14 = m[A11] * m[A22] - m[A21] * m[A12];
	float SubFactor15 = m[A10] * m[A23] - m[A20] * m[A13];
	float SubFactor16 = m[A10] * m[A22] - m[A20] * m[A12];
	float SubFactor17 = m[A10] * m[A21] - m[A20] * m[A11];

	mat4 Inverse;
	Inverse[A00] = + (m[A11] * SubFactor00 - m[A12] * SubFactor01 + m[A13] * SubFactor02);
	Inverse[A01] = - (m[A10] * SubFactor00 - m[A12] * SubFactor03 + m[A13] * SubFactor04);
	Inverse[A02] = + (m[A10] * SubFactor01 - m[A11] * SubFactor03 + m[A13] * SubFactor05);
	Inverse[A03] = - (m[A10] * SubFactor02 - m[A11] * SubFactor04 + m[A12] * SubFactor05);
	Inverse[A10] = - (m[A01] * SubFactor00 - m[A02] * SubFactor01 + m[A03] * SubFactor02);
	Inverse[A11] = + (m[A00] * SubFactor00 - m[A02] * SubFactor03 + m[A03] * SubFactor04);
	Inverse[A12] = - (m[A00] * SubFactor01 - m[A01] * SubFactor03 + m[A03] * SubFactor05);
	Inverse[A13] = + (m[A00] * SubFactor02 - m[A01] * SubFactor04 + m[A02] * SubFactor05);
	Inverse[A20] = + (m[A01] * SubFactor06 - m[A02] * SubFactor07 + m[A03] * SubFactor08);
	Inverse[A21] = - (m[A00] * SubFactor06 - m[A02] * SubFactor09 + m[A03] * SubFactor10);
	Inverse[A22] = + (m[A00] * SubFactor07 - m[A01] * SubFactor09 + m[A03] * SubFactor11);
	Inverse[A23] = - (m[A00] * SubFactor08 - m[A01] * SubFactor10 + m[A02] * SubFactor11);
	Inverse[A30] = - (m[A01] * SubFactor12 - m[A02] * SubFactor13 + m[A03] * SubFactor14);
	Inverse[A31] = + (m[A00] * SubFactor12 - m[A02] * SubFactor15 + m[A03] * SubFactor16);
	Inverse[A32] = - (m[A00] * SubFactor13 - m[A01] * SubFactor15 + m[A03] * SubFactor17);
	Inverse[A33] = + (m[A00] * SubFactor14 - m[A01] * SubFactor16 + m[A02] * SubFactor17);

	float Determinant =
		+ m[A00] * Inverse[A00]
		+ m[A01] * Inverse[A01]
		+ m[A02] * Inverse[A02]
		+ m[A03] * Inverse[A03];

	int i;
	for (i = 0; i < 16; i ++)
		Inverse[i] /= Determinant;

	memcpy(res, Inverse, sizeof Inverse);
}

/* perspective projection (https://www.khronos.org/registry/OpenGL-Refpages/gl2.1/xhtml/gluPerspective.xml) */
void matPerspective(mat4 res, float fov_deg, float aspect, float znear, float zfar)
{
	memset(res, 0, sizeof (mat4));

	double q = 1 / tan(fov_deg * M_PI / 360);
	res[A00] = q / aspect;
	res[A11] = q;
	res[A22] = (znear + zfar) / (znear - zfar);
	res[A23] = 2 * znear * zfar / (znear - zfar);
	res[A32] = -1;
}

/* orthographic projection */
void matOrtho(mat4 res, float left, float right, float top, float bottom, float znear, float zfar)
{
	memset(res, 0, sizeof (mat4));
	res[A00] = 2 / (right - left);
	res[A11] = 2 / (top - bottom);
	res[A22] = 1 / (zfar - znear);
	res[A03] = - (right + left) / (right - left);
	res[A13] = - (top + bottom) / (bottom - top);
	res[A23] = - znear / (zfar - znear);
	res[A33] = 1;
}

/* similar to gluLookAt */
void matLookAt(mat4 res, float eyeX,  float eyeY,  float eyeZ, float centerX, float centerY, float centerZ, float upX, float upY, float upZ)
{
	vec4 fwd = {centerX - eyeX, centerY - eyeY, centerZ - eyeZ};
	vec4 up = {upX, upY, upZ};
	vec4 side;

	memset(res, 0, sizeof (mat4));

	vec4 eye = {eyeX, eyeY, eyeZ};
	vecNormalize(fwd, fwd);
	vecCrossProduct(side, fwd, up);
	vecNormalize(side, side);
	vecCrossProduct(up, side, fwd);
	vecNormalize(up, up);

	/* from book */
	res[A00] = side[VX];
	res[A01] = side[VY];
	res[A02] = side[VZ];
	res[A03] = - vecDotProduct(side, eye);
	res[A10] = up[VX];
	res[A11] = up[VY];
	res[A12] = up[VZ];
	res[A13] = - vecDotProduct(up, eye);
	res[A20] = -fwd[VX];
	res[A21] = -fwd[VY];
	res[A22] = -fwd[VZ];
	res[A23] = vecDotProduct(fwd, eye);
	res[A33] = 1;
}

/* generate a transformation matrix in res */
void matIdent(mat4 res)
{
	memset(res, 0, sizeof (mat4));
	res[A00] = res[A11] = res[A22] = res[A33] = 1;
}

void matTranslate(mat4 res, float x, float y, float z)
{
	matIdent(res);
	res[A03] = x;
	res[A13] = y;
	res[A23] = z;
}

void matScale(mat4 res, float x, float y, float z)
{
	memset(res, 0, sizeof *res);
	res[A00] = x;
	res[A11] = y;
	res[A22] = z;
	res[A33] = 1;
}

void matRotate(mat4 res, float theta, int axis_0X_1Y_2Z)
{
	float fcos = cosf(theta);
	float fsin = sinf(theta);
	matIdent(res);
	switch (axis_0X_1Y_2Z) {
	case 0: /* along X axis */
		res[A11] = fcos;
		res[A12] = -fsin;
		res[A21] = fsin;
		res[A22] = fcos;
		break;
	case 1: /* along Y axis */
		res[A00] = fcos;
		res[A02] = -fsin;
		res[A20] = fsin;
		res[A22] = fcos;
		break;
	case 2: /* along Z axis */
		res[A00] = fcos;
		res[A01] = -fsin;
		res[A10] = fsin;
		res[A11] = fcos;
	}
}

void matPrint(mat4 A)
{
	int i;
	fputc('[', stderr);
	for (i = 0; i < 16; i ++)
	{
		fprintf(stderr, "\t%f", A[i]);
		if ((i & 3) == 3) fputc('\n', stderr);
	}
	fputs("];\n", stderr);
}

/*
 * classical vector operations
 */
void vecAdd(vec4 res, vec4 A, vec4 B)
{
	res[VX] = A[VX] + B[VX];
	res[VY] = A[VY] + B[VY];
	res[VZ] = A[VZ] + B[VZ];
}

void vecSub(vec4 res, vec4 A, vec4 B)
{
	res[VX] = A[VX] - B[VX];
	res[VY] = A[VY] - B[VY];
	res[VZ] = A[VZ] - B[VZ];
}

float vecLength(vec4 A)
{
	return sqrt(A[VX]*A[VX] + A[VY]*A[VY] + A[VZ]*A[VZ]);
}

void vecNormalize(vec4 res, vec4 A)
{
	float len = vecLength(A);
	res[VX] = A[VX] / len;
	res[VY] = A[VY] / len;
	res[VZ] = A[VZ] / len;
}

float vecDotProduct(vec4 A, vec4 B)
{
	return A[VX]*B[VX] + A[VY]*B[VY] + A[VZ]*B[VZ];
}

/* get perpendicular vector to A and B */
void vecCrossProduct(vec4 res, vec4 A, vec4 B)
{
	vec4 tmp;

	tmp[VX] = A[VY]*B[VZ] - A[VZ]*B[VY];
	tmp[VY] = A[VZ]*B[VX] - A[VX]*B[VZ];
	tmp[VZ] = A[VX]*B[VY] - A[VY]*B[VX];

	memcpy(res, tmp, sizeof tmp);
}
