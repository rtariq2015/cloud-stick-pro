#pragma once
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
#ifdef __cplusplus
extern "C" {
#endif
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t,const char*,uint32_t,void*,int,TaskHandle_t*,int);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t);
#ifdef __cplusplus
}
#endif
