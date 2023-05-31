#pragma once

/* This file was originally copied from components/modules/ai/who_human_face_recognition.hpp */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

void register_face_recognition(QueueHandle_t frame_i,
                                     QueueHandle_t result,
                                     QueueHandle_t frame_o = NULL,
                                     const bool camera_fb_return = false);
