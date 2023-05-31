#include "create_json.h"
#include "esp_log.h"

#define TAG "JSON"


#if CONFIG_MFN_V1
#if CONFIG_S8
char* create_json(cJSON **root, uint8_t device_id, int8_t * data, uint16_t data_length)
#elif CONFIG_S16
char* create_json(cJSON **root, uint8_t device_id, float * data, uint16_t data_length)
#endif
#endif
{
#if CONFIG_MFN_V1
#if CONFIG_S8
    const char* data_type = "int8"; // or "int8"
#elif CONFIG_S16
    const char* data_type = "float"; // or "float"
#endif
#endif

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create json object");
        return NULL;
    }

    // Create a cJSON root object
    *root = cJSON_CreateObject();
    if(*root == NULL || data == NULL || data_length <= 0)
    {
        ESP_LOGE(TAG, "Failed to create json object");
        return NULL;
    }

    // Add data to cJSON object
    cJSON_AddNumberToObject(*root, "device_id", device_id);
    cJSON_AddStringToObject(*root, "data_type", data_type);
    cJSON_AddNumberToObject(*root, "data_length", data_length);

    ESP_LOGW("JSON", "Length of data=%d", data_length);

    // Create a cJSON data array and add it to the root object
#if CONFIG_MFN_V1
#if CONFIG_S8
    cJSON *data_array = cJSON_CreateIntArray(data, data_length);
#elif CONFIG_S16
    cJSON *data_array = cJSON_CreateFloatArray(data, data_length);
#endif
#endif
    if(data_array == NULL)
    {
        cJSON_Delete(*root); // Delete root object to free memory
        ESP_LOGE(TAG, "Failed to create json data array");
        return NULL;
    }

    cJSON_AddItemToObject(*root, "data", data_array);

    // Print the JSON string
    char *json_string = cJSON_Print(*root);
    if(json_string == NULL)
    {
        cJSON_Delete(*root); // Delete root object to free memory
        ESP_LOGE(TAG, "Failed to create json string");
        return NULL;
    }

    // ESP_LOGI(TAG, "Created JSON string: %s", json_string);
    return json_string;
}

void free_json(cJSON *root, char *json_string)
{
    // Free memory
    cJSON_free(json_string);
    cJSON_Delete(root);
}