#include <Arduino.h>
#include <freertos/semphr.h>
#include "heap_operation_guard.h"

namespace HeapOperationGuard {
namespace {
SemaphoreHandle_t operationMutex = nullptr;
}

void begin()
{
    if (!operationMutex) operationMutex = xSemaphoreCreateMutex();
    if (!operationMutex) Serial.println("Heap operation mutex unavailable");
}

Lock::Lock(bool wait)
{
    // Continue operating if mutex allocation itself ever fails; the firmware
    // remains functional, although without the concurrency protection.
    acquired_ = !operationMutex ||
                xSemaphoreTake(operationMutex, wait ? portMAX_DELAY : 0) == pdTRUE;
}

Lock::~Lock()
{
    if (operationMutex && acquired_) xSemaphoreGive(operationMutex);
}

}
