#include "who_camera.h"
#include "who_human_face_recognition.hpp"
#include "wifi_task.h"
#include "dl_variable.hpp"
#include "esp_log.h"
#include "create_json.h"

static QueueHandle_t xQueueAIFrame = NULL;
static QueueHandle_t xQueueFacePrints = NULL;

#define GPIO_BOOT GPIO_NUM_0

static void printface(void*params)
{
    dl::Tensor<float> faceprint;
    while(true)
    {
        if (xQueueFacePrints == NULL )
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        else
        {
            ESP_LOGE("printface", "1: min free stack is %d", uxTaskGetStackHighWaterMark(NULL));
            xQueueReceive(xQueueFacePrints, &faceprint, portMAX_DELAY);
            ESP_LOGE("printface", "2: min free stack is %d", uxTaskGetStackHighWaterMark(NULL));
            // float test = 12.0;
            cJSON * root = NULL;
            char* json_string = NULL;

            // char* create_json(cJSON **root, uint8_t device_id, const float * const data, uint16_t data_length);
            // json_string = create_json(&root, (uint8_t)0, &test, (uint16_t)1);
            json_string = create_json(&root, 0, faceprint.element, faceprint.get_size());
            if(root == NULL)
            {
                ESP_LOGE("JSON", "Failed to create json object");
            }
            ESP_LOGE("printface", "3: min free stack is %d", uxTaskGetStackHighWaterMark(NULL));
            ESP_LOGE("app_main", "Print json: '''\n%s\n'''", json_string);
            ESP_LOGE("printface", "4: min free stack is %d", uxTaskGetStackHighWaterMark(NULL));
            free_json(root, json_string);
            root = NULL;
            json_string = NULL;
            // faceprint.print_all();
        }
    } // while true
}

extern "C" void app_main()
{
    xQueueAIFrame = xQueueCreate(1, sizeof(camera_fb_t *));
    xQueueFacePrints = xQueueCreate(2, sizeof(dl::Tensor<float>));

    /* Setup LED GPIO4 as ouput */
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
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    // Could try increasing camera resolution here but device runs out of RAM if any higher than FRAMESIZE_QVGA is selected
    register_camera(PIXFORMAT_RGB565, FRAMESIZE_QVGA, 2, xQueueAIFrame);
    // // register_camera(PIXFORMAT_RGB565, FRAMESIZE_CIF, 2, xQueueAIFrame);

    register_human_face_recognition(xQueueAIFrame, xQueueFacePrints, NULL, true);
    
    // Device ID controls which device in this is in the order of all devices doing face print productions
    // A Device ID of 0 is for the very first device in the line doing the faceprint productions.
    // In our projet, there are only two device ID's. Any >0 value for the 2nd (END Device) ID will work and signify the end of the line
    // faceprint detection.
    /* CONFIGURE THIS */
    const uint8_t device_id = 0;
    register_wifi(device_id, xQueueFacePrints);
    // xTaskCreatePinnedToCore(printface, "printface", 4 * 1024, NULL, 6, NULL, 0);
}
