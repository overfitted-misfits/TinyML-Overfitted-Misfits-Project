#include "who_human_face_recognition.hpp"

#include "esp_log.h"
#include "esp_camera.h"

#include "dl_image.hpp"
#include "fb_gfx.h"

#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "face_recognition_tool.hpp"

#if CONFIG_MFN_V1
#if CONFIG_S8
#include "face_recognition_112_v1_s8.hpp"
#elif CONFIG_S16
#include "face_recognition_112_v1_s16.hpp"
#endif
#endif

#include "who_ai_utils.hpp"
#include "driver/gpio.h"

using namespace std;
using namespace dl;

static const char *TAG = "human_face_recognition";

static QueueHandle_t xQueueFrameI = NULL;
static QueueHandle_t xQueueEvent = NULL;
static QueueHandle_t xQueueFrameO = NULL;
static QueueHandle_t xQueueResult = NULL;

static recognizer_state_t gEvent = DETECT;
static bool gReturnFB = true;
static face_info_t recognize_result;

SemaphoreHandle_t xMutex;

typedef enum
{
    SHOW_STATE_IDLE,
    SHOW_STATE_DELETE,
    SHOW_STATE_RECOGNIZE,
    SHOW_STATE_ENROLL,
} show_state_t;

#define RGB565_MASK_RED 0xF800
#define RGB565_MASK_GREEN 0x07E0
#define RGB565_MASK_BLUE 0x001F
#define FRAME_DELAY_NUM 16

static void rgb_print(camera_fb_t *fb, uint32_t color, const char *str)
{
    fb_gfx_print(fb, (fb->width - (strlen(str) * 14)) / 2, 10, color, str);
}

static int rgb_printf(camera_fb_t *fb, uint32_t color, const char *format, ...)
{
    char loc_buf[64];
    char *temp = loc_buf;
    int len;
    va_list arg;
    va_list copy;
    va_start(arg, format);
    va_copy(copy, arg);
    len = vsnprintf(loc_buf, sizeof(loc_buf), format, arg);
    va_end(copy);
    if (len >= sizeof(loc_buf))
    {
        temp = (char *)malloc(len + 1);
        if (temp == NULL)
        {
            return 0;
        }
    }
    vsnprintf(temp, len + 1, format, arg);
    va_end(arg);
    rgb_print(fb, color, temp);
    if (len > 64)
    {
        free(temp);
    }
    return len;
}

static void task_process_handler(void *arg)
{
    camera_fb_t *frame = NULL;
    //  * @param score_threshold   predicted boxes with score lower than the threshold will be filtered out
    //  * @param nms_threshold     predicted boxes with IoU higher than the threshold will be filtered out
    //  * @param top_k             first k highest score boxes will be remained
    //  * @param resize_scale      resize scale to implement on input image
    HumanFaceDetectMSR01 detector(0.3F, 0.3F, 10, 0.3F);
    HumanFaceDetectMNP01 detector2(0.4F, 0.3F, 10);
    // HumanFaceDetectMSR01 detector(0.1F, 0.5F, 10, 0.2F);
    // HumanFaceDetectMNP01 detector2(0.5F, 0.3F, 5);

#if CONFIG_MFN_V1
#if CONFIG_S8
    ESP_LOGE("DAH", "Face Recognition using INT8 Quantized Model");
    FaceRecognition112V1S8 *recognizer = new FaceRecognition112V1S8();
#elif CONFIG_S16
    ESP_LOGE("DAH", "Face Recognition using FLOAT16 Quantized Model");
    FaceRecognition112V1S16 *recognizer = new FaceRecognition112V1S16();
#endif
#endif
    show_state_t frame_show_state = SHOW_STATE_IDLE;
    recognizer_state_t _gEvent;
    recognizer->set_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fr");
    int partition_result = recognizer->set_ids_from_flash();

    int count = 0;
    bool saveRec = true;
    while (true)
    {
        xSemaphoreTake(xMutex, portMAX_DELAY);
        _gEvent = gEvent;
        gEvent = DETECT;
        xSemaphoreGive(xMutex);

        if (_gEvent)
        {
            bool is_detected = false;

            if (xQueueReceive(xQueueFrameI, &frame, portMAX_DELAY))
            {
                std::list<dl::detect::result_t> &detect_candidates = detector.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
                std::list<dl::detect::result_t> &detect_results = detector2.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_candidates);

                int i = 0;
                for (std::list<dl::detect::result_t>::iterator face_result = detect_results.begin(); face_result != detect_results.end(); face_result++, i++)
                {
                // }

                // while (detect_results.size() > 0)
                // {
                    ESP_LOGI("DAH", "results=%d", i);
                    // is_detected = true;

                    ESP_LOGE("DAH", "Detected %d people and %d cadidates with frame height=%d and width=%d", detect_results.size(), detect_candidates.size(), (int)frame->height, (int)frame->width);
                // }

                // if (is_detected)
                // {
                    // count++;
                    // gpio_set_level(GPIO_NUM_4, count%2);
                    ESP_LOGW("DAH", "%d Enrolled ids", recognizer->get_enrolled_ids().size());
                    if (recognizer->get_enrolled_ids().size() <= 2)
                    {
                        recognizer->enroll_id((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, face_result->keypoint, "", true);
                        ESP_LOGE("ENROLL", "ID %d is enrolled", recognizer->get_enrolled_ids().back().id);
                        recognizer->get_face_emb().print_all();
                        recognizer->get_face_emb().print_shape();
                    }
                    else
                    {
                        ESP_LOGW("RECOGNIZE", "Do recognition");
                        recognize_result = recognizer->recognize((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, face_result->keypoint);
                        // print_detection_result(detect_results);
                        if (recognize_result.id > 0)
                        {
                            ESP_LOGW("RECOGNIZE", "Similarity: %f, Match ID: %d", recognize_result.similarity, recognize_result.id);
                        }
                        else
                        {
                            ESP_LOGW("RECOGNIZE", "Detected person was not recognized");
                        }

                    }

                    // switch (_gEvent)
                    // {
                    // case ENROLL:
                    //     ESP_LOGW("ENROLL", "ID %d is enrolled", recognizer->get_enrolled_ids().back().id);
                    //     frame_show_state = SHOW_STATE_ENROLL;
                    //     break;

                    // case RECOGNIZE:
                    //     recognize_result = recognizer->recognize((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_results.front().keypoint);
                    //     print_detection_result(detect_results);
                    //     if (recognize_result.id > 0)
                    //         ESP_LOGI("RECOGNIZE", "Similarity: %f, Match ID: %d", recognize_result.similarity, recognize_result.id);
                    //     else
                    //         ESP_LOGE("RECOGNIZE", "Similarity: %f, Match ID: %d", recognize_result.similarity, recognize_result.id);
                    //     frame_show_state = SHOW_STATE_RECOGNIZE;
                    //     break;

                    // case DELETE:
                    //     vTaskDelay(10);
                    //     recognizer->delete_id(true);
                    //     ESP_LOGE("DELETE", "% d IDs left", recognizer->get_enrolled_id_num());
                    //     frame_show_state = SHOW_STATE_DELETE;
                    //     break;

                    // default:
                    //     break;
                    // }
                }

                if (detect_results.size() <= 0)
                {
                    ESP_LOGE("DAH:", "No person detected!");
                }

                // if (frame_show_state != SHOW_STATE_IDLE)
                // {
                //     static int frame_count = 0;
                //     switch (frame_show_state)
                //     {
                //     case SHOW_STATE_DELETE:
                //         rgb_printf(frame, RGB565_MASK_RED, "%d IDs left", recognizer->get_enrolled_id_num());
                //         break;

                //     case SHOW_STATE_RECOGNIZE:
                //         if (recognize_result.id > 0)
                //             rgb_printf(frame, RGB565_MASK_GREEN, "ID %d", recognize_result.id);
                //         else
                //             rgb_print(frame, RGB565_MASK_RED, "who ?");
                //         break;

                //     case SHOW_STATE_ENROLL:
                //         rgb_printf(frame, RGB565_MASK_BLUE, "Enroll: ID %d", recognizer->get_enrolled_ids().back().id);
                //         break;

                //     default:
                //         break;
                //     }

                //     if (++frame_count > FRAME_DELAY_NUM)
                //     {
                //         frame_count = 0;
                //         frame_show_state = SHOW_STATE_IDLE;
                //     }
                // }

//                 if (detect_results.size())
//                 {
// #if !CONFIG_IDF_TARGET_ESP32S3
//                     print_detection_result(detect_results);
// #endif
//                     draw_detection_result((uint16_t *)frame->buf, frame->height, frame->width, detect_results);
//                 }
            }

            if (xQueueFrameO)
            {

                xQueueSend(xQueueFrameO, &frame, portMAX_DELAY);
            }
            else if (gReturnFB)
            {
                esp_camera_fb_return(frame);
            }
            else
            {
                free(frame);
            }

            if (xQueueResult && is_detected)
            {
                xQueueSend(xQueueResult, &recognize_result, portMAX_DELAY);
            }
        }
    }
}

static void task_event_handler(void *arg)
{
    recognizer_state_t _gEvent;
    while (true)
    {
        xQueueReceive(xQueueEvent, &(_gEvent), portMAX_DELAY);
        xSemaphoreTake(xMutex, portMAX_DELAY);
        gEvent = _gEvent;
        xSemaphoreGive(xMutex);
    }
}

void register_human_face_recognition(const QueueHandle_t frame_i,
                                     const QueueHandle_t event,
                                     const QueueHandle_t result,
                                     const QueueHandle_t frame_o,
                                     const bool camera_fb_return)
{
        uint32_t press_code;
    // zero-initialize the config structure.
    gpio_config_t io_conf = {};
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO4
    io_conf.pin_bit_mask = (1ULL<<4);
    //disable pull-down mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    //disable pull-up mode
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    //configure GPIO with the given settings
    gpio_config(&io_conf);


    xQueueFrameI = frame_i;
    xQueueFrameO = frame_o;
    xQueueEvent = event;
    xQueueResult = result;
    gReturnFB = camera_fb_return;
    xMutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(task_process_handler, TAG, 5 * 1024, NULL, 5, NULL, 0);
    if (xQueueEvent)
        xTaskCreatePinnedToCore(task_event_handler, TAG, 4 * 1024, NULL, 5, NULL, 1);
}
