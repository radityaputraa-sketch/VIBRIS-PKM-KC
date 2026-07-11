#include "DualCoreTaskScheduler.h"
#include "FFTProcessor.h"
#include "config.h"
#include "MultiSensorFeatureMerger.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static QueueHandle_t vibrationQueue = NULL;
static volatile float latestRPM = 0.0f;
static float latestBandEnergies[4] = {0.0f, 0.0f, 0.0f, 0.0f};

QueueHandle_t Scheduler_GetVibrationQueue() {
    return vibrationQueue;
}

static void TaskFFTProcessor(void *pvParameters) {
    static VibrationBuffer incomingBuffer;
    float rpmResult = 0.0f;
    float bandEnergies[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    SensorFeatures fftLocalFeatures;

    FFTProcessor_Init();

    for (;;) {
        if (xQueueReceive(vibrationQueue, &incomingBuffer, portMAX_DELAY) == pdTRUE) {
            FFTProcessor_Process(&incomingBuffer, &fftLocalFeatures, &rpmResult, bandEnergies);
            latestRPM = rpmResult;
            for (int i = 0; i < 4; i++) latestBandEnergies[i] = bandEnergies[i];

            updateVibrationFeature(fftLocalFeatures.rms_getaran);

            Serial.printf("[FFT] RPM=%.1f | Unbalance=%.2f | Misalign=%.2f\n",
                          rpmResult, bandEnergies[0], bandEnergies[1]);
        }
    }
}

void Scheduler_InitTasks() {
    vibrationQueue = xQueueCreate(2, sizeof(VibrationBuffer));

    xTaskCreatePinnedToCore(
        TaskFFTProcessor, "Task_FFT", STACK_TASK_FFT, NULL,
        PRIO_TASK_FFT, NULL, CORE_SYSTEM_SLOW_IO
    );
}

float Scheduler_GetLatestRPM() {
    return latestRPM;
}
void Scheduler_GetLatestBandEnergies(float *dest) {
    for (int i = 0; i < 4; i++) dest[i] = latestBandEnergies[i];
}
