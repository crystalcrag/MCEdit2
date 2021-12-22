/*
 * utils.h : classical datatypes and helper functions for opengl
 */


#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include "UtilityLibLite.h"

#define RESDIR              "resources/"
#define INTERFACE           "interface/"
#define SKYDIR              "skydome/"
#define SHADERDIR           "shaders/"
#define EPSILON             0.0001f

typedef uint16_t *          DATA16;
typedef uint32_t *          DATA32;
typedef struct NVGcontext * NVGCTX;
typedef int16_t *           DATAS16;

typedef float *             vec;
typedef float               vec4[4];
typedef float               mat4[16];

typedef struct              /* structure needed by glMultiDrawArraysIndirect() */
{
	int count;              /* number of vertex (not bytes) to process in this draw call */
	int instanceCount;      /* nb of items to draw */
	int first;              /* first vertex to process */
	int baseInstance;       /* starting index in buffer, setup by glVertexAttribDivisor */

}	MDAICmd_t;

typedef MDAICmd_t *    MDAICmd;

#define MDAI_SIZE           16


enum /* stored column first like GLSL [A<row><col>] */
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

int  createGLSLProgram(const char * vertexShader, const char * fragmentShader, const char * geomShader);
int  createGLSLProgramCond(const char * vertexShader, const char * fragmentShader, const char * inject);
int  checkOpenGLError(const char * name);
void setShaderValue(int prog, const char * field, int args, float * array);

#define bitfieldExtract(num, start, length)    ((num >> start) & ((1 << length)-1))
#define ROT4(num)                              (((num << 1) & 15) | ((num & 8) >> 3))
#define M_PIf                                  3.1415926535f
#define M_PI_2f                                1.5707963267f
#define M_PI_4f                                0.7853981633f
#define M_SQRT1_2f                             0.7071067811f
#define DEG_TO_RAD                             (M_PIf / 180)
#define RAD_TO_DEG                             (180 / M_PIf)

/* Q'n'D JSON parser */
typedef Bool (*JSONParseCb_t)(const char * file, STRPTR * keys, int line);
Bool   jsonParse(const char * file, JSONParseCb_t cb);
STRPTR jsonValue(STRPTR * keys, STRPTR key);
int    jsonParseString(DATA8 dst, DATA8 src, int max);

/* res can point to A or B */
void matTranspose(mat4 A);
void matAdd(mat4 res, mat4 A, mat4 B);
void matMult(mat4 res, mat4 A, mat4 B);
void matMult3(mat4 res, mat4 A, mat4 B);
void matMultByVec(vec4 res, mat4 A, vec4 B);
void matMultByVec3(vec4 res, mat4 A, vec4 B);
void matPreMultByVec3(vec4 res, mat4 A, vec4 B);
void matInverseTranspose(mat4 res, mat4 A);
Bool matInverse(mat4 res, mat4 A);

/* generate a transformation matrix in res */
void matTranslate(mat4 res, float x, float y, float z);
void matScale(mat4 res, float x, float y, float z);
void matRotate(mat4 res, float theta, int axis_0X_1Y_2Z);
void matIdent(mat4 res);

/* perspective matrix */
void matPerspective(mat4 res, float fov_deg, float aspect, float znear, float zfar);
void matOrtho(mat4 res, float left, float right, float bottom, float top, float znear, float zfar);
void matLookAt(mat4 res, vec4 eye, vec4 center, vec4 up);

void matPrint(mat4 A);

/* vector operation (res can point to A or B) */
float vecLength(vec4 A);
void  vecNormalize(vec4 res, vec4 A);
float vecDotProduct(vec4 A, vec4 B);
void  vecCrossProduct(vec4 res, vec4 A, vec4 B);
void  vecSub(vec4 res, vec4 A, vec4 B);
void  vecAdd(vec4 res, vec4 A, vec4 B);
float vecDistSquare(vec4 A, vec4 B);
float normAngle(float angle);

#define vecAddNum(A, num) \
	(A)[VX] += num, (A)[VY] += num, (A)[VZ] += num

#define vec3Add(A, B) \
	(A)[VX] += (B)[VX], \
	(A)[VY] += (B)[VY], \
	(A)[VZ] += (B)[VZ]

#define vec3AddMult(A, B, num) \
	(A)[VX] = ((A)[VX] * num) + (B)[VX], \
	(A)[VY] = ((A)[VY] * num) + (B)[VY], \
	(A)[VZ] = ((A)[VZ] * num) + (B)[VZ]

/* dynamic array */
#define vectorNth    vector_nth
void * vectorPush(vector, void * item);
void * vectorPushTop(vector);

typedef void (*PostProcess_t)(DATA8 * data, int * w, int * h, int bpp);

/* texture load */
int  textureLoad(const char * dir, const char * name, int clamp, PostProcess_t);
int  textureCheckboard(int w, int h, int cellsz, DATA8 color1, DATA8 color2);
int  textureLoadCubeMap(const char * basename, int single);
int  textureGen(DATA8 data, int w, int h, int bpp);
void textureDump(int glTex, int w, int h);

/* texture save */
int textureSavePNG(const char * path, DATA8 pixels, int stride, int width, int height, int bpp);
int textureConvertToCMap(DATA8 bitmap, int width, int height);

/* misc. */
#ifdef __GNUC__
#define popcount     __builtin_popcount
#else
int  popcount(uint32_t);
#endif
int  roundToUpperPrime(int n);
int  roundToLowerPrime(int n);
void DOS2Unix(STRPTR path);

/* free the entire table (suppose v is stack allocated) */
#define vectorFree           vector_free
#define vectorInit           vector_init
#define vectorInitFill       vector_init_fill
#define vectorInitZero       vector_init_zero
#define vectorFirst          vector_first
#define vectorLast(v)        vector_nth(&(v), (v).count - 1)
#define vectorReset          vector_reset

#endif
