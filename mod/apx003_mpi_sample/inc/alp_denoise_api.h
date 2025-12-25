/*********************************

EVS DENOISE V1.0

*********************************/
#ifndef ALP_DENOISE_API
#define ALP_DENOISE_API

#ifdef _WIN32
#ifdef API_EXPORTS
#define ALP_ALGO_DLL_API_C _declspec(dllexport)
#else
#define ALP_ALGO_DLL_API_C _declspec(dllimport)
#endif
#else
#define ALP_ALGO_DLL_API_C
#endif

// #include <vector>
#include <math.h>
// #include <string>
#include <string.h>

// typedef enum
// {
//     DENOISE_ERROR = 0,
//     DENOISE_MEMORY_ERROR = -1,
//     DENOISE_PARAM_ERROR = -2,
//     DENOISE_RIGHT = 1,

// } DENOISEErrCode;

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void *alp_handle_api_t;
#if 0
    // ALP_ALGO_DLL_API_C int initTemporalDenoise(alp_handle_api_t &handle, uint16_t height, uint16_t width, uint8_t evs_zero);

    // ALP_ALGO_DLL_API_C int runTemporalDenoise(alp_handle_api_t handle, uint8_t *in_raw, uint8_t threshold);

    // ALP_ALGO_DLL_API_C int releaseTemporalDenoise(alp_handle_api_t handle);

    // ALP_ALGO_DLL_API_C int runSpatialDenoise(const uint8_t *in_raw, uint8_t *out_raw, uint16_t height, uint16_t width, uint8_t evs_zero, uint8_t kernel_size, uint8_t threshold);

    // ALP_ALGO_DLL_API_C int runSpatialDenoiseSimple(uint8_t *in_raw, uint16_t height, uint16_t width, uint8_t evs_zero, uint8_t threshold = 1);

    // ALP_ALGO_DLL_API_C int initSpatialTemporalSimple(alp_handle_api_t &handle, uint16_t height, uint16_t width, uint8_t evs_zero);

    // ALP_ALGO_DLL_API_C int runSpatialTemporalSimple(alp_handle_api_t handle, uint8_t *in_raw, uint8_t spatial_threshold, uint8_t temporal_threshold);

    // ALP_ALGO_DLL_API_C int releaseSpatialTemporalSimple(alp_handle_api_t handle);
#else
    ALP_ALGO_DLL_API_C int initTemporalDenoise(alp_handle_api_t *handle, uint16_t height, uint16_t width, uint8_t evs_zero);

    ALP_ALGO_DLL_API_C int runTemporalDenoise(alp_handle_api_t handle, uint8_t *in_raw, uint8_t threshold);

    ALP_ALGO_DLL_API_C int releaseTemporalDenoise(alp_handle_api_t handle);

    ALP_ALGO_DLL_API_C int runSpatialDenoise(const uint8_t *in_raw, uint8_t *out_raw, uint16_t height, uint16_t width, uint8_t evs_zero, uint8_t kernel_size, uint8_t threshold);

    ALP_ALGO_DLL_API_C int runSpatialDenoiseSimple(uint8_t *in_raw, uint16_t height, uint16_t width, uint8_t evs_zero, uint8_t threshold);

    ALP_ALGO_DLL_API_C int initSpatialTemporalSimple(alp_handle_api_t *handle, uint16_t height, uint16_t width, uint8_t evs_zero);

    ALP_ALGO_DLL_API_C int runSpatialTemporalSimple(alp_handle_api_t handle, uint8_t *in_raw, uint8_t spatial_threshold, uint8_t temporal_threshold);

    ALP_ALGO_DLL_API_C int releaseSpatialTemporalSimple(alp_handle_api_t handle);
#endif

#ifdef __cplusplus
}
#endif

#endif
