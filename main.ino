#include "config.h"
#include "SharedTypes.h"
#include "DriverSuhu.h"
#include "DriverArus.h"
#include "DriverGetaran.h"
#include "DriverINM.h"
#include "DualCoreTaskScheduler.h"
#include "MultiSensorFeatureMerger.h"
#include "MahalanobisDetector.h"
#include "InitialBaselineCalibrator.h"
#include "AdaptiveBaselineLearner.h"
#include "DiagnosisClassifier.h"
#include "RaspberryPiDataTransmitter.h"


#define PLOTTER_MODE 0
#define RAW_TEST_MODE 1  // 1 = skip kalibrasi, stream raw sensor buat testing. WAJIB 0 sebelum testing deteksi anomali beneran.
bool systemCalibrated = false;

float bandBaselineMean[4];
float bandBaselineStd[4];

void setup() {
    Transmitter_Init(115200);
    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[SYSTEM] Booting Clean Modular Sensor Core..."));

    xTaskCreatePinnedToCore(TaskDriverINM, "Task_INM", STACK_TASK_INM, NULL, PRIO_TASK_INM, NULL, CORE_DSP_HIGH_SPEED);
    Scheduler_InitTasks();
    xTaskCreatePinnedToCore(TaskDriverGetaran, "Task_Vib", 3072, NULL, PRIO_TASK_FFT, NULL, CORE_DSP_HIGH_SPEED);
    xTaskCreatePinnedToCore(TaskDriverArus, "Task_Arus", STACK_TASK_ARUS, NULL, PRIO_TASK_ARUS, NULL, CORE_DSP_HIGH_SPEED);
    xTaskCreatePinnedToCore(TaskDriverSuhu, "Task_Suhu", STACK_TASK_SUHU, NULL, PRIO_TASK_SUHU, NULL, CORE_SYSTEM_SLOW_IO);

    Serial.println(F("[SYSTEM] Boot Complete. Core Dedicated Sync."));
}

void loop() {
#if RAW_TEST_MODE
    SensorFeatures merged;
    getMergedFeatures(&merged);

    DetectionResult dummyResult;
    dummyResult.rpm_estimated = 0.0f;
    dummyResult.mahalanobis_D2 = 0.0f;
    strncpy(dummyResult.status_label, "RAW_TEST", sizeof(dummyResult.status_label) - 1);

    Transmitter_SendResult(merged, dummyResult);

    Serial.printf("[RAW] Vib=%.4f Snd=%.2f Arus=%.4f Suhu=%.2f Valid=%d\n",
        merged.rms_getaran, merged.rms_suara, merged.arus, merged.suhu, merged.valid);

    vTaskDelay(pdMS_TO_TICKS(TICK_DELAY_REPORT));
    return;
#endif

    if (!systemCalibrated) {
        float flashMean[4];
        float flashSigmaInv[4][4];

        bool sensorLoaded = loadBaselineFromFlash(flashMean, flashSigmaInv);
        bool bandLoaded = sensorLoaded && loadBandBaselineFromFlash(bandBaselineMean, bandBaselineStd);

        if (sensorLoaded && bandLoaded) {
            initializeBaselineLearner(flashMean, flashSigmaInv);
            systemCalibrated = true;
            Serial.println(F("[SYSTEM] Baseline berhasil dimuat dari NVS Flash. Mode Deteksi Aktif."));
        } else {
            Serial.println(F("[SYSTEM] Flash Empty/Incomplete. Memulai Fase Kalibrasi Otomatis (60 Detik)..."));
            startCalibrationPhase();

            for (int i = 0; i < 60; i++) {
                SensorFeatures currentSample;
                if (getMergedFeatures(&currentSample)) {
                    addCalibrationSample(currentSample);
                    float currentBands[4];
                    Scheduler_GetLatestBandEnergies(currentBands);
                    addBandEnergyCalibrationSample(currentBands);
                    Serial.printf("[CALIBRATION] %d/60 | Getaran=%.4f Suara=%.2f Arus=%.4f Suhu=%.2f\n",
                        i + 1, currentSample.rms_getaran, currentSample.rms_suara,
                        currentSample.arus, currentSample.suhu);
                } else {
                    Serial.println(F("[CALIBRATION] Sample dilewati — data sensor stale/belum fresh."));
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

            computeInitialBaseline(flashMean, flashSigmaInv);

            if (!isLastCalibrationValid()) {
                Serial.println(F("[SYSTEM] KALIBRASI GAGAL — mesin tidak aktif/variance terlalu rendah. Ulangi dengan motor menyala."));
                vTaskDelay(pdMS_TO_TICKS(5000));
                return;
            }

            saveBaselineToFlash(flashMean, flashSigmaInv);
            computeBandEnergyBaseline(bandBaselineMean, bandBaselineStd);
            saveBandBaselineToFlash(bandBaselineMean, bandBaselineStd);
            initializeBaselineLearner(flashMean, flashSigmaInv);
            systemCalibrated = true;
            Serial.println(F("[SYSTEM] Kalibrasi Awal Selesai & Tersimpan. Masuk Mode Deteksi."));
        }
    }

    DetectionResult detResult = runDetectionCycle();

    char diagnosisLabel[32] = "NORMAL";
    float severityConfidence = 0.0f;
    float currentBandEnergies[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    if (strcmp(detResult.status_label, "Waspada") == 0 || strcmp(detResult.status_label, "Bahaya") == 0) {
        Scheduler_GetLatestBandEnergies(currentBandEnergies);
        Diagnosis_Classify(currentBandEnergies, bandBaselineMean, bandBaselineStd, diagnosisLabel, &severityConfidence);
    }
    getMergedFeatures(&merged);

    Transmitter_SendResult(merged, detResult);

#if PLOTTER_MODE
    Serial.printf("Suhu:%.2f Arus:%.4f Getaran:%.4f Suara:%.2f D2:%.2f Status:%s\n",
        merged.suhu, merged.arus, merged.rms_getaran, merged.rms_suara, detResult.mahalanobis_D2, detResult.status_label);
#else
    Serial.printf("\n================= TELEMETRI MONITORING =================");
    Serial.printf("\nRPM ESTIMATED : %7.2f RPM", detResult.rpm_estimated);
    Serial.printf("\nMAHALANOBIS D2: %7.2f (Threshold Max: 9.49)", detResult.mahalanobis_D2);
    Serial.printf("\nANOMALY STATE : %s", detResult.status_label);
    if (strcmp(detResult.status_label, "Normal") != 0) {
        Serial.printf("\nDIAGNOSIS     : %s (Z-Score Max: %.2f)", diagnosisLabel, severityConfidence);
    }
    Serial.printf("\n------------------- DATA MENTAH SENSOR -----------------");
    Serial.printf("\nGETARAN (RMS) : %7.4f m/s2", merged.rms_getaran);
    Serial.printf("\nSUARA (RMS)   : %7.2f", merged.rms_suara);
    Serial.printf("\nARUS MOTOR    : %7.4f A", merged.arus);
    Serial.printf("\nSUHU OPERASI  : %7.2f C", merged.suhu);
    Serial.printf("\n========================================================\n");
#endif

    vTaskDelay(pdMS_TO_TICKS(TICK_DELAY_REPORT));
}