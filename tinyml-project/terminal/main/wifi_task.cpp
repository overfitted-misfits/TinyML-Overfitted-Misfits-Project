#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include <sys/param.h>
#include "esp_netif.h"
#include "lwip/sockets.h"
#include <lwip/netdb.h>
#include <arpa/inet.h>
#include "nvs_flash.h"
#include "create_json.h"
#include "dl_variable.hpp"

#include "wifi_task.h"

// Don't change this; Selects the WIFI Auth Type
#ifdef CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif


/* Prototypes */
static void tcp_client_task(void *pvParameters);
static void wifi_init_sta(void);
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);


/* Globals */
static QueueHandle_t wifi_xQueueFacePrints = NULL;
static uint8_t DEVICE_ID = 0;


/* Function Definitions */

void register_wifi(uint8_t device_id, QueueHandle_t xQueueFacePrints)
{
    //Initialize NVS used by WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    wifi_init_sta();

    DEVICE_ID = device_id;
    wifi_xQueueFacePrints = xQueueFacePrints;

    xTaskCreate(tcp_client_task, "tcp_client", 10*1024, NULL, 5, NULL);
}

void tcp_client_task(void *pvParameters)
{
    char rx_buffer[128];
    char host_ip[] = HOST_IP_ADDR;
    int addr_family = 0;
    int ip_protocol = 0;
    int retc;

    while (1)
    {
        // vTaskDelay(5000 / portTICK_PERIOD_MS);
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;

        int sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, PORT);

        retc = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in));
        if (retc != 0)
        {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Successfully connected");

        // static const char *payload = "Message from ESP32 ";
        // static float *data = faceprint.element;
        char *payload_json_string = NULL;
        dl::Tensor<float> faceprint;

        cJSON * root = NULL;
        // ESP_LOGE(TAG, "1: min free stack is %d", uxTaskGetStackHighWaterMark(NULL));

        while (1)
        {
            // ESP_LOGE(TAG, "2: min free stack is %d", uxTaskGetStackHighWaterMark(NULL));
            // ESP_LOGI(TAG, "Pop from queue");
            xQueueReceive(wifi_xQueueFacePrints, &faceprint, portMAX_DELAY);
            // ESP_LOGE(TAG, "3: min free stack is %d", uxTaskGetStackHighWaterMark(NULL));
            // ESP_LOGI(TAG, "Create JSON");
            payload_json_string = create_json(&root, DEVICE_ID, faceprint.element, faceprint.get_size());

            // ESP_LOGE(TAG, "4: min free stack is %d", uxTaskGetStackHighWaterMark(NULL));
            if (root == NULL || payload_json_string == NULL)
            {
                ESP_LOGE(TAG, "Error occurred creating JSON!");
                break;
            }

            ESP_LOGI(TAG, "Send JSON faceprint data via socket");
            retc = send(sock, WIFI_HEADER_JSON_START, strlen(WIFI_HEADER_JSON_START), 0);
            retc = send(sock, payload_json_string,    strlen(payload_json_string),    0);
            retc = send(sock, WIFI_HEADER_JSON_END,   strlen(WIFI_HEADER_JSON_END),   0);
            // ESP_LOGE(TAG, "5: min free stack is %d", uxTaskGetStackHighWaterMark(NULL));
            if (retc < 0)
            {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                break;
            }

            // ESP_LOGI(TAG, "Free JSON");
            free_json(root, payload_json_string);
            root = NULL;
            payload_json_string = NULL;

            // int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // // Error occurred during receiving
            // if (len < 0)
            // {
            //     ESP_LOGE(TAG, "recv failed: errno %d", errno);
            //     break;
            // }
            // // Data received
            // else
            // {
            //     rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
            //     ESP_LOGI(TAG, "Received %d bytes from %s:", len, host_ip);
            //     ESP_LOGI(TAG, "%s", rx_buffer);
            // }

            // vTaskDelay(2000 / portTICK_PERIOD_MS);
        }

        if (sock != -1)
        {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    static int s_retry_num = 0;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {

        if (s_retry_num < WIFI_RECONNECT_MAX_RETRYS)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else
    {
        ESP_LOGW(TAG, "Unhandled Event");
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid,      WIFI_SSID);
    strcpy((char*)wifi_config.sta.password,  WIFI_PASS);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits( s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE,
                                            pdFALSE,
                                            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID: '%s' password: '%s'",
                 WIFI_SSID, WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID: '%s', password: '%s'",
                 WIFI_SSID, WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}
