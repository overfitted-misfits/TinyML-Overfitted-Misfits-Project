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
#include "dl_variable.hpp"

using namespace std;
using namespace dl;

static QueueHandle_t xQueueFrameI = NULL;
static QueueHandle_t xQueueEvent = NULL;
static QueueHandle_t xQueueFrameO = NULL;
static QueueHandle_t xQueueResult = NULL;

static bool gReturnFB = true;
static std::vector<Tensor<float>> recognize_result_tensor;
static face_info_t recognize_result;

SemaphoreHandle_t xMutex;

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
    recognizer->set_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fr");
    int partition_result = recognizer->set_ids_from_flash();
    int i;

    while (true)
    {
        bool is_detected = false;

        // Receive camera frame or wait until one is available
        if (xQueueReceive(xQueueFrameI, &frame, portMAX_DELAY))
        {
            std::list<dl::detect::result_t> &detect_candidates = detector.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
            std::list<dl::detect::result_t> &detect_results = detector2.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_candidates);

            // ESP_LOGE("DAH", "START");
            ESP_LOGI("DAH", "Detected %d people and %d cadidates with frame height=%d and width=%d; currently %d Enrolled ids", detect_results.size(), detect_candidates.size(), (int)frame->height, (int)frame->width, recognizer->get_enrolled_ids().size());

            if (detect_results.size() > 0)
            {
                i = 0;
                for (std::list<dl::detect::result_t>::iterator face_result = detect_results.begin(); face_result != detect_results.end(); face_result++, i++)
                {
                    ESP_LOGW("DAH", "Processing detected person #%d of %d", i+1, detect_results.size());
                    ESP_LOGW("RECOGNIZE", "Do recognition");

                    // Perform recognition against face prints/embeddings enrolled. However, none should ever be enrolled.
                    // We do this to make the model compute the faceprint/embeddeding because no function call is exposed to do it otherwise.
                    recognize_result = recognizer->recognize((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, face_result->keypoint);

                    // Get reference to faceprint/embedding
                    Tensor<float>& tensor = recognizer->get_face_emb();

                    // Add faceprint/embedding to result queue to be processed by another task
                    if ( xQueueResult != NULL )
                    {
                        // Push faceprint to queue. If queue is busy for >500ms, skip this faceprint
                        BaseType_t retc = xQueueSend(xQueueResult, &tensor, 500 / portTICK_PERIOD_MS);
                        if ( retc != pdPASS )
                        {
                            ESP_LOGE("DAH", "Queue Overflow! Failed to push faceprint to xQueueResult queue!");
                        }
                    } // If queue is available

                    /**
                     * Can Enable or Disable this as desired. Simply enrolls the first 3 faceprints
                     */
                    if (recognizer->get_enrolled_ids().size() < 3)
                    {
                        ESP_LOGW("ENROLL", "Do enrollment of face embedding");
                        // Enroll the faceprint/embedding
                        recognizer->enroll_id(tensor, to_string(recognizer->get_enrolled_ids().size()), false);
                    } // if enrolled ids < 3

                    if (recognize_result.id > 0)
                    {
                        ESP_LOGW("RECOGNIZE", "Similarity: %f, Match ID: %d", recognize_result.similarity, recognize_result.id);
                    }
                    else
                    {
                        ESP_LOGW("RECOGNIZE", "Person was not recognized");
                    }
                } // For each detected person
            }
            else
            {
                ESP_LOGW("DAH:", "No person detected!");
            } // else no person detected

            // ESP_LOGE("DAH", "END");
        }

        // If out queue exists, pass the image frame to the queue
        if (xQueueFrameO)
        {
            // ESP_LOGE("DAH", "Sending frame to out queue");
            xQueueSend(xQueueFrameO, &frame, portMAX_DELAY);
        }
        // Else ask the camera to free it
        else if (gReturnFB)
        {
            // ESP_LOGE("DAH", "Return frame");
            esp_camera_fb_return(frame);
        }
        else
        {
            // ESP_LOGE("DAH", "Frame free memory");
            free(frame);
        }
    }
}

void register_human_face_recognition(const QueueHandle_t frame_i,
                                     const QueueHandle_t result,
                                     const QueueHandle_t frame_o,
                                     const bool camera_fb_return)
{
    xQueueFrameI = frame_i;
    xQueueFrameO = frame_o;
    xQueueResult = result;
    gReturnFB = camera_fb_return;

    xTaskCreatePinnedToCore(task_process_handler, "face_recognition_task", 5 * 1024, NULL, 5, NULL, 0);
}
