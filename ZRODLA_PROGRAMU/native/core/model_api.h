#pragma once
#include <stdint.h>
#ifdef _WIN32
#define MS3D_MODEL_API __declspec(dllexport)
#else
#define MS3D_MODEL_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Each UTF-8 response belongs to this DLL. Release with ms3d_model_free.
MS3D_MODEL_API char* ms3d_model_call(const char* request);
MS3D_MODEL_API void ms3d_model_free(void* response);
MS3D_MODEL_API uint32_t ms3d_model_version(void);
typedef struct MS3DPartPose {
    double origin[3];
    double angles[3];
    double placement[4]; // X, Y, Z, yaw of the entire furniture.
    uint32_t plane; // 0 XY, 1 XZ, 2 YZ
} MS3DPartPose;
// Transform all vertices using one precomputed affine transform. No JSON.
// Inputs/output are count * 3 doubles. Returns winding sign, or 0 on error.
MS3D_MODEL_API int ms3d_model_transform(const MS3DPartPose* pose,
    const double* input, uint64_t count, double* output);
#ifdef __cplusplus
}
#endif
