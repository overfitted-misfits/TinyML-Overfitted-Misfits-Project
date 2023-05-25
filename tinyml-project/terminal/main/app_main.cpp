#include "who_camera.h"
#include "who_human_face_recognition.hpp"
#include "who_button.h"
#include "event_logic.hpp"
#include "who_adc_button.h"
#include "dl_variable.hpp"

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
            xQueueReceive(xQueueFacePrints, &faceprint, portMAX_DELAY);
            ESP_LOGE("app_main", "Print faceprint");
            faceprint.print_all();
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

    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set,e.g.GPIO4
    io_conf.pin_bit_mask = (1ULL<<0);
    //disable pull-down mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    //disable pull-up mode
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    // Could try increasing camera resolution here but device runs ou tof RAM if any higher than FRAMESIZE_QVGA is selected
    register_camera(PIXFORMAT_RGB565, FRAMESIZE_QVGA, 2, xQueueAIFrame);
    // register_camera(PIXFORMAT_RGB565, FRAMESIZE_CIF, 2, xQueueAIFrame);
    register_human_face_recognition(xQueueAIFrame, xQueueFacePrints, NULL, true);
    
        xTaskCreatePinnedToCore(printface, "printface", 4 * 1024, NULL, 6, NULL, 0);
}
