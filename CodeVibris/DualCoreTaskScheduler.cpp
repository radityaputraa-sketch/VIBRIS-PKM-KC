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
static float latestRmsX = 0.0f;
static float latestRmsZ = 0.0f;
static float latestRmsY = 0.0f;

static void TaskFFTProcessor(void *pvParameters) {
    static VibrationBuffer incomingBuffer;
    float rpmResult = 0.0f;
    float bandEnergies[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    SensorFeatures fftLocalFeatures;

    // PERBAIKAN NOISE GETARAN: sama pola dengan fix arus sebelumnya -- nilai
    // RMS getaran & per-axis (X/Y/Z) yang dikirim ke dashboard/TinyML itu
    // mentah per-batch (tiap ~0.5 detik), belum pernah dihaluskan sama
    // sekali. Variansi statistik antar-batch + noise mekanis alami
    // accelerometer bikin grafik di dashboard kelihatan "berisik". EMA di
    // sini HANYA menghaluskan nilai yang DIKIRIM KELUAR (dashboard/TinyML),
    // TIDAK menyentuh fftLocalFeatures.rms_getaran mentah yang sudah dipakai
    // di dalam FFTProcessor_Process buat gerbang reliability RPM (supaya
    // logic RPM yang sudah di-tune sebelumnya tidak ikut berubah perilakunya).
    static bool vibSmoothInit = false;
    static float smoothedVibRms = 0.0f;
    static float smoothedX = 0.0f, smoothedY = 0.0f, smoothedZ = 0.0f;
    // Alpha lebih besar dari arus (0.4 vs 0.25) SENGAJA: getaran dipakai
    // buat deteksi anomali Bahaya/Waspada yang perlu tetap responsif kalau
    // ada lonjakan getaran mendadak beneran -- jangan terlalu dihaluskan
    // sampai lambat mendeteksi kondisi bahaya sungguhan.
    const float VIB_SMOOTHING_ALPHA = 0.4f;

    FFTProcessor_Init();

    for (;;) {
        if (xQueueReceive(vibrationQueue, &incomingBuffer, portMAX_DELAY) == pdTRUE) {
            FFTProcessor_Process(&incomingBuffer, &fftLocalFeatures, &rpmResult, bandEnergies);
            latestRPM = rpmResult;

            if (!vibSmoothInit) {
                smoothedVibRms = fftLocalFeatures.rms_getaran;
                smoothedX = incomingBuffer.rms_x_raw;
                smoothedY = incomingBuffer.rms_y_raw;
                smoothedZ = incomingBuffer.rms_z_raw;
                vibSmoothInit = true;
            } else {
                smoothedVibRms = VIB_SMOOTHING_ALPHA * fftLocalFeatures.rms_getaran + (1.0f - VIB_SMOOTHING_ALPHA) * smoothedVibRms;
                smoothedX      = VIB_SMOOTHING_ALPHA * incomingBuffer.rms_x_raw     + (1.0f - VIB_SMOOTHING_ALPHA) * smoothedX;
                smoothedY      = VIB_SMOOTHING_ALPHA * incomingBuffer.rms_y_raw     + (1.0f - VIB_SMOOTHING_ALPHA) * smoothedY;
                smoothedZ      = VIB_SMOOTHING_ALPHA * incomingBuffer.rms_z_raw     + (1.0f - VIB_SMOOTHING_ALPHA) * smoothedZ;
            }

            latestRmsX = smoothedX;
            latestRmsZ = smoothedZ;
            latestRmsY = smoothedY;
            for (int i = 0; i < 4; i++) latestBandEnergies[i] = bandEnergies[i];

            updateVibrationFeature(smoothedVibRms);

            Serial.printf("[FFT] RPM=%.1f |  Y_RMS=%.4f | X_RMS=%.4f | Z_RMS=%.4f | Unbalance=%.2f | Misalign=%.2f\n",
                          rpmResult, smoothedVibRms, latestRmsX, latestRmsZ, bandEnergies[0], bandEnergies[1]);
        }
    }
}
void Scheduler_GetLatestAxisRMS(float *xOut, float *yOut, float *zOut) {
    *xOut = latestRmsX;
    *yOut = latestRmsY;
    *zOut = latestRmsZ;
}
void Scheduler_InitTasks() {
    vibrationQueue = xQueueCreate(1, sizeof(VibrationBuffer));

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
