#include "who_human_face_detection.hpp"

#include "esp_log.h"
#include "esp_camera.h"

#include "dl_image.hpp"
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

#define TWO_STAGE_ON 1

static const char *TAG = "human_face_detection";

static QueueHandle_t xQueueFrameI = NULL;
static QueueHandle_t xQueueEvent = NULL;
static QueueHandle_t xQueueFrameO = NULL;
static QueueHandle_t xQueueResult = NULL;

static bool gEvent = true;
static bool gReturnFB = true;

#if CONFIG_MFN_V1
#if CONFIG_S8
static void perform_face_recognition(FaceRecognition112V1S8 *recognizer, std::list<dl::detect::result_t>* detected_results, camera_fb_t *frame)
#elif CONFIG_S16
static void perform_face_recognition(FaceRecognition112V1S16 *recognizer, std::list<dl::detect::result_t>* detected_results, camera_fb_t *frame)
#endif
#endif
{
    assert(recognizer != NULL);
    assert(detected_results != NULL);
    assert(frame != NULL);

    int i = 0;
    for (std::list<dl::detect::result_t>::iterator face_result = detected_results->begin(); face_result != detected_results->end(); face_result++, i++)
    {
    // }

    // while (detected_results->size() > 0)
    // {
        ESP_LOGI("DAH", "recognition loop=%d", i);
        // is_detected = true;

        ESP_LOGE("DAH", "Detected %d people with frame height=%d and width=%d", detected_results->size(), (int)frame->height, (int)frame->width);
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
            static face_info_t recognize_result;
            recognize_result = recognizer->recognize((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, face_result->keypoint);
            // print_detection_result(*detected_results);
            if (recognize_result.id > 0)
            {
                ESP_LOGI("RECOGNIZE", "Similarity: %f, Match ID: %d", recognize_result.similarity, recognize_result.id);
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
        //     recognize_result = recognizer->recognize((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detected_results->front().keypoint);
        //     print_detection_result(*detected_results);
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

    if (detected_results->size() <= 0)
    {
        ESP_LOGE("DAH:", "No person detected!");
    }
}

static void task_process_handler(void *arg)
{
    camera_fb_t *frame = NULL;
    HumanFaceDetectMSR01 detector(0.3F, 0.3F, 10, 0.3F);
#if TWO_STAGE_ON
    HumanFaceDetectMNP01 detector2(0.4F, 0.3F, 10);
#endif

#if CONFIG_MFN_V1
#if CONFIG_S8
    ESP_LOGE("DAH", "Face Recognition using INT8 Quantized Model");
    FaceRecognition112V1S8 *recognizer = new FaceRecognition112V1S8();
#elif CONFIG_S16
    ESP_LOGE("DAH", "Face Recognition using FLOAT16 Quantized Model");
    FaceRecognition112V1S16 *recognizer = new FaceRecognition112V1S16();
#endif
    recognizer->set_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fr");
    recognizer->set_ids_from_flash();
#endif

#if TWO_STAGE_ON
    ESP_LOGE("DAH", "Face Recognition TWO Stage ON");
#else
    ESP_LOGE("DAH", "Face Recognition SINGLE STAE");
#endif

static face_info_t recognize_result;

    while (true)
    {
        if (gEvent)
        {
            bool is_detected = false;
            if (xQueueReceive(xQueueFrameI, &frame, portMAX_DELAY))
            {
#if TWO_STAGE_ON
                std::list<dl::detect::result_t> &detect_candidates = detector.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
                std::list<dl::detect::result_t> &detect_results = detector2.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_candidates);
#else
                std::list<dl::detect::result_t> &detect_results = detector.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
#endif

                if (detect_results.size() > 0)
                {
                    draw_detection_result((uint16_t *)frame->buf, frame->height, frame->width, detect_results);
                    print_detection_result(detect_results);
                    is_detected = true;
                }
                // Wanted to put this after the xQueueSend() so that the HTTP server updates the camera feed,
                // but the issue is that the HTTP task will clear/free the frame memory. The solution would be
                // to write a task that does the recognition separate from detection and then pass the frame through
                // the HTTP (easy to do) and also return the dl::detect::result_t to a new queue to the recognition task.
                // I guess for now it will just have to do the recognition before updating the HTTP server feed.
#if CONFIG_MFN_V1
                if (detect_results.size() > 0)
                {
                    perform_face_recognition(recognizer, &detect_results, frame);
                }
#endif
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

            if (xQueueResult)
            {
                xQueueSend(xQueueResult, &is_detected, portMAX_DELAY);
            }
        }
    }
}

static void task_event_handler(void *arg)
{
    while (true)
    {
        xQueueReceive(xQueueEvent, &(gEvent), portMAX_DELAY);
    }
}

void register_human_face_detection(const QueueHandle_t frame_i,
                                   const QueueHandle_t event,
                                   const QueueHandle_t result,
                                   const QueueHandle_t frame_o,
                                   const bool camera_fb_return)
{
    xQueueFrameI = frame_i;
    xQueueFrameO = frame_o;
    xQueueEvent = event;
    xQueueResult = result;
    gReturnFB = camera_fb_return;

    xTaskCreatePinnedToCore(task_process_handler, TAG, 5 * 1024, NULL, 5, NULL, 0);
    if (xQueueEvent)
        xTaskCreatePinnedToCore(task_event_handler, TAG, 4 * 1024, NULL, 5, NULL, 1);
}
