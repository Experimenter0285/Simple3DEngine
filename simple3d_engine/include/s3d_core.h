#ifndef S3D_CORE_H
#define S3D_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

// Basic types
typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef signed short int16;
typedef unsigned int uint32;
typedef signed int int32;
typedef unsigned long long uint64;
typedef signed long long int64;
typedef float float32;
typedef double float64;

// Boolean
typedef int bool32;
#define S3D_TRUE 1
#define S3D_FALSE 0

// Result codes
typedef enum {
    S3D_OK = 0,
    S3D_ERROR = -1,
    S3D_ERROR_INIT = -2,
    S3D_ERROR_RENDER = -3,
    S3D_ERROR_PLATFORM = -4,
    S3D_ERROR_MEMORY = -5,
} s3d_result;

// Utility macros
#define S3D_UNUSED(x) ((void)(x))

// Version info
#define S3D_VERSION_MAJOR 1
#define S3D_VERSION_MINOR 0
#define S3D_VERSION_PATCH 0

#ifdef __cplusplus
}
#endif

#endif // S3D_CORE_H
