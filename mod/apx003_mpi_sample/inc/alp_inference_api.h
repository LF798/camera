#ifndef ALP_INFERENCE_API_H
#define ALP_INFERENCE_API_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief ALP_PERSON_DVS_NUMB_MAX_SIZE 64
 * The max number of person dvs detection
 *
 */
#define ALP_MAX_OUTPUT_NUM 64
#define ALP_MIN_PERSON_HEIGHT_RATIO 0.25
#define ALP_MAX_PERSON_HEIGHT_RATIO 1.0

    /**
     * @brief Model type
     *  RV1106_DVS_WxH_CH1
     *
     */
    enum model_type_api
    {
        RV1106_DVS_320x320_INT8,
        RV1106_DVS_384x320_INT8,
        RV1106_APS_640x320,
        RK3588_DVS_768x608_INT8,
    };

    /**
     * @brief Image pixel format
     *
     */
    typedef enum image_format_api_t
    {
        ALP_IMAGE_FORMAT_GRAY8,
        ALP_IMAGE_FORMAT_RGB888,
        ALP_IMAGE_FORMAT_RGBA8888,
        ALP_IMAGE_FORMAT_YUV420SP_NV21,
        ALP_IMAGE_FORMAT_YUV420SP_NV12,
    } image_format_api_t;

    /**
     * @brief Input buffer
     *
     */
    typedef struct alp_input_api_t
    {
        int width;
        int height;
        int width_stride;
        int height_stride;
        image_format_api_t format;
        unsigned char *virt_addr;
        int size;
        int fd;
    } alp_input_api_t;

    /**
     * @brief Class name
     */

    typedef enum class_name_api_t
    {
        person,
    } class_name_api_t;

    /**
     * @brief Output rectangle
     *
     */
    typedef struct alp_box_api_t
    {
        float score; //[0,1]
        int x1;
        int y1;
        int x2;
        int y2;
        int id;
        int cls;
    } alp_box_api_t;

    typedef void *alp_handle_api_t;

    // detect interface
    int Init_Alp_Detector_Api(alp_handle_api_t *handle, char *model_path, int model_type);
    int Run_Alp_Detector_Api(alp_handle_api_t handle, alp_input_api_t *input, alp_box_api_t **output, int *output_num);
    int Release_Alp_Detector_Api(alp_handle_api_t handle);

    // denoise interface
    int Init_Alp_Denoise_Api(alp_handle_api_t *handle, char *model_path, int model_type);
    int Run_Alp_Denoise_Api(alp_handle_api_t handle, alp_input_api_t *input, int threshold);
    int Release_Alp_Denoise_Api(alp_handle_api_t handle);

#ifdef __cplusplus
} // extern "C" {
#endif

#endif // ALP_INFERENCE_API_H