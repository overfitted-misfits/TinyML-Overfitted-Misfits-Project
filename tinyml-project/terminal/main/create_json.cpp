#include "create_json.h"
#include "esp_log.h"

#define TAG "JSON"

char* create_json(cJSON **root, uint8_t device_id, float * data, uint16_t data_length)
{
    // Device parameters
    // int device_id = 1234;
    const char* data_type = "float"; // or "int8"
    
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create json object");
        return NULL;
    }

    // Array data
    // int data_length = 10;
    // float data[data_length];

    // Create a cJSON root object
    // cJSON * root = NULL;
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
    // for (uint16_t i = 0; i < data_length; ++i)
    // {
    //     ESP_LOGE("JSON", "%f ", data[i]);
    // }
    
    // Create a cJSON data array and add it to the root object
    cJSON *data_array = cJSON_CreateFloatArray(data, data_length);
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