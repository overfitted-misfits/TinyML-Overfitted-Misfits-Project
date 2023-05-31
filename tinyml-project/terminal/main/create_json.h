#pragma once

#include "cJSON.h"
#include <stdint.h>

#if CONFIG_MFN_V1
#if CONFIG_S8
char* create_json(cJSON **root, uint8_t device_id, int8_t * data, uint16_t data_length);
#elif CONFIG_S16
char* create_json(cJSON **root, uint8_t device_id, float * data, uint16_t data_length);
#endif
#endif

void free_json(cJSON *root, char *json_string);
