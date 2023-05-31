#pragma once

// #include "freertos/event_groups.h"
#include "esp_event.h"

/* CONFIGURE THESE WIFI SETTINGS!!! */
#define WIFI_SSID      "thebitches"
#define WIFI_PASS      "happymochi!"
//@brief set the config that selects the WiFi Auth type from below 
// #define CONFIG_ESP_WIFI_AUTH_OPEN       1
#define CONFIG_ESP_WIFI_AUTH_WPA3_PSK   1

#define WIFI_RECONNECT_MAX_RETRYS  10000
#define HOST_IP_ADDR               "10.0.0.19"
#define PORT                       40000
#define TAG                        "WIFI"

// The HEADER prepended to JSON string that is transmitted
#define WIFI_HEADER_JSON_START     ("======START_JSON_MESSAGE======")
// The HEADER postpended to JSON string that is transmitted
#define WIFI_HEADER_JSON_END       ("------END_JSON_MESSAGE------")


/* Public Function Prototypes */

void register_wifi(uint8_t device_id, QueueHandle_t xQueueFacePrints);
