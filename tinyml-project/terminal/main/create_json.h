#pragma once

#include "cJSON.h"
#include <stdint.h>

char* create_json(cJSON **root, uint8_t device_id, float * data, uint16_t data_length);

void free_json(cJSON *root, char *json_string);
