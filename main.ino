#include "config.h"
#include "SharedTypes.h"
#include "DriverSuhu.h"
#include "DriverArus.h"
#include "DriverGetaran.h"
#include "DriverINM.h"
#include "DualCoreTaskScheduler.h"
#include "MultiSensorFeatureMerger.h"
#include "GenericThresholdClassifier.h"
#include "RaspberryPiDataTransmitter.h"
#include "DiagnosisClassifier.h"
#include "MahalanobisDetector.h"
#include "InitialBaselineCalibrator.h"   
#include "AdaptiveBaselineLearner.h" 
#include "RPMEstimator.h"
#include "FFTProcessor.h"

// Catatan perubahan (biar klean lain paham kenapa file ini beda
// dari versi sebelumnya):
// Versi lama menunggu fase KALIBRASI 60 detik (self-baseline + Mahalanobis)
// sebelum status Normal/Waspada/Bahaya bisa muncul. Di lapangan itu bikin
// dashboard "diam" 1 menit tiap kali device baru nyala/dipindah ke mesin
// lain, dan begitu kalibrasi selesai, seluruh status malah nyangkut di
// "Bahaya" terus (baseline hasil kalibrasi singkat gampang rusak/tidak stabil
// -> Mahalanobis D2 jadi meledak untuk data yang sebenarnya normal).
//
// Versi ini melewati proses kalibrasi itu sepenuhnya: begitu perangkat
// menyala, tiap sample sensor langsung diklasifikasi memakai ambang batas
// TETAP (lihat GenericThresholdClassifier.cpp), jadi dashboard langsung
// menampilkan grafik + status sejak detik pertama, di mesin/lokasi mana pun
// modul sensor dipasang.
//
// Tambahan: beberapa sensor (mic INMP441, buffer FFT getaran, dll.) butuh
// beberapa detik pertama untuk mengisi buffer sebelum datanya "fresh".
// Tanpa penanganan khusus, beberapa cycle pertama setelah boot akan
// dilaporkan sebagai "SensorFault" padahal sensor sebenarnya baik-baik saja,
// cuma belum sempat mengambil sample pertama. WARMUP_GRACE_MS memberi
// toleransi waktu itu supaya status yang tampil di dashboard saat baru
// dibuka adalah "Warming" (wajar), bukan "SensorFault" (menyesatkan seolah
// ada yang rusak). Kalau setelah masa toleransi ini data masih belum fresh
// juga, baru dianggap SensorFault sungguhan (sensor/kabel bermasalah).
#define WARMUP_GRACE_MS 8000
#define PLOTTER_MODE 0

static unsigned long bootMillis = 0;
static char groundTruthLabel[16] = "NORMAL";

static float bandBaselineMean[4] = {0.20f, 0.20f, 0.20f, 0.20f};
static float bandBaselineStd[4]  = {0.10f, 0.10f, 0.10f, 0.10f};
#define CALIBRATION_DURATION_MS 30000UL   // 30 detik NYATA (millis()), bukan hitungan sample
static unsigned long calibrationStartMillis = 0;
void setup() {
    setDiagnosisBandBaseline(bandBaselineMean, bandBaselineStd);
    Transmitter_Init(115200);
    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[SYSTEM] Booting Clean Modular Sensor Core..."));

    xTaskCreatePinnedToCore(TaskDriverINM, "Task_INM", STACK_TASK_INM, NULL, PRIO_TASK_INM, NULL, CORE_DSP_HIGH_SPEED);
    Scheduler_InitTasks();
    xTaskCreatePinnedToCore(TaskDriverGetaran, "Task_Vib", 3072, NULL, PRIO_TASK_VIB, NULL, CORE_DSP_HIGH_SPEED);
    xTaskCreatePinnedToCore(TaskDriverArus,"Task_Arus", STACK_TASK_ARUS,NULL, PRIO_TASK_ARUS, NULL, CORE_SYSTEM_SLOW_IO);
    xTaskCreatePinnedToCore(TaskDriverSuhu, "Task_Suhu", STACK_TASK_SUHU, NULL, PRIO_TASK_SUHU, NULL, CORE_SYSTEM_SLOW_IO);
 
    bootMillis = millis();
    startCalibrationPhase();
    calibrationStartMillis = millis();
    Serial.println(F("[SYSTEM] Boot Complete. Memulai fase kalibrasi self-baseline (180 detik nyata)."));
}

void loop() {
    SensorFeatures merged{};
    bool fresh = getMergedFeatures(&merged);
    bool stillWarmingUp = (millis() - bootMillis) < WARMUP_GRACE_MS;

    DetectionResult result{};
    result.rpm_estimated  = Scheduler_GetLatestRPM();
    result.mahalanobis_D2 = 0.0f;
    strncpy(result.diagnosis_label, "N/A", sizeof(result.diagnosis_label) - 1);
    result.diagnosis_label[sizeof(result.diagnosis_label) - 1] = '\0';
    result.diagnosis_confidence = 0.0f;

    bool calibrationTimeUp = (millis() - calibrationStartMillis) >= CALIBRATION_DURATION_MS;

// Baca command sederhana dari Raspberry Pi/laptop: 1 karakter per command
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'B') {          // 'B' = mesin ini punya rolling bearing
            setBearingType(true);
            Serial.println(F("[CMD] Bearing type: ROLLING"));
        } else if (cmd == 'N') {   // 'N' = mesin ini bushing/no rolling bearing
            setBearingType(false);
            Serial.println(F("[CMD] Bearing type: BUSHING/NONE"));
        } else if (cmd == 'O') {   // 'O' = ground truth OK/normal (kondisi motor asli tanpa fault)
            strncpy(groundTruthLabel, "NORMAL", sizeof(groundTruthLabel) - 1);
            Serial.println(F("[TEST] Ground truth: NORMAL"));
        } else if (cmd == 'U') {   // 'U' = ground truth unbalance sengaja dipasang
            strncpy(groundTruthLabel, "UNBALANCE", sizeof(groundTruthLabel) - 1);
            Serial.println(F("[TEST] Ground truth: UNBALANCE"));
        } else if (cmd == 'M') {   // 'M' = ground truth misalignment sengaja dipasang
            strncpy(groundTruthLabel, "MISALIGNMENT", sizeof(groundTruthLabel) - 1);
            Serial.println(F("[TEST] Ground truth: MISALIGNMENT"));
        } else if (cmd == 'F') {   // 'F' = ground truth bearing fault disimulasikan
            strncpy(groundTruthLabel, "BEARING_FAULT", sizeof(groundTruthLabel) - 1);
            Serial.println(F("[TEST] Ground truth: BEARING_FAULT"));
        }
    }
    if (!fresh && stillWarmingUp) {
        strncpy(result.status_label, "Warming", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
    } else if (!fresh) {
        strncpy(result.status_label, "SensorFault", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
    } else if (!isBaselineLearnerReady() && !calibrationTimeUp) {
        // FIX: gerbang kalibrasi berbasis WAKTU NYATA (millis()), bukan jumlah
        // sample -- rate loop() terbukti tidak konstan di lapangan. Semua
        // sample yang berhasil ditangkap dalam jendela 180 detik ini dipakai,
        // sebanyak apapun jumlahnya (tergantung rate riil setelah fix DriverArus).
        addCalibrationSample(merged);
        addSNRCalibrationSample(Scheduler_GetLatestSNR());

        float bandEnergies[4];
        Scheduler_GetLatestBandEnergies(bandEnergies);
        addBandEnergyCalibrationSample(bandEnergies);

        strncpy(result.status_label, "Calibrating", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
    } else if (!isBaselineLearnerReady()) {
        float mean[4], stdDev[4], sigmaInv[4][4];
        computeInitialBaseline(mean, sigmaInv);

        if (isLastCalibrationValid()) {
            getFeatureStdDev(stdDev);
            initializeBaselineLearner(mean, stdDev, sigmaInv);
            saveBaselineToFlash(mean, sigmaInv);

            // TAMBAHAN: baseline band frekuensi sekarang dihitung dari data
            // kalibrasi NYATA, bukan placeholder 0.20/0.10 selamanya.
            float bandMean[4], bandStd[4];
            computeBandEnergyBaseline(bandMean, bandStd);
            setDiagnosisBandBaseline(bandMean, bandStd);
            saveBandBaselineToFlash(bandMean, bandStd);
            setRuntimeSNRThreshold(computeSNRThresholdFromCalibration());
            Serial.println(F("[SYSTEM] Kalibrasi VALID. Baseline mean/sigma dan band energy siap."));
        } else {
            Serial.println(F("[SYSTEM] Kalibrasi GAGAL (varians terlalu rendah). Mengulang 180 detik..."));
            startCalibrationPhase();
            calibrationStartMillis = millis();
        }
        strncpy(result.status_label, "Calibrating", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
    } else {
        result = runDetectionCycle();
    }

    Transmitter_SendResult(merged, result, groundTruthLabel);

#if DEBUG_BAND_ENERGY_MODE
        // Nyalakan mode ini SEMENTARA saat mesin dalam kondisi NORMAL untuk
        // mengumpulkan angka mean & std band energy yang benar. Matikan lagi
        // (kembalikan ke 0) setelah bandBaselineMean/Std di atas sudah diisi
        // angka hasil kalibrasi manual.
        Serial.printf("[BAND_ENERGY] E0=%.4f E1=%.4f E2=%.4f E3=%.4f\n",
            bandEnergies[0], bandEnergies[1], bandEnergies[2], bandEnergies[3]);
#endif
#if PLOTTER_MODE
    Serial.printf("Suhu:%.2f Arus:%.4f Getaran:%.4f Suara:%.2f Status:%s\n",
        merged.suhu, merged.arus, merged.rms_getaran, merged.rms_suara, result.status_label);
#else
    Serial.printf("\n================= TELEMETRI MONITORING =================");
    Serial.printf("\nRPM ESTIMATED : %7.2f RPM", result.rpm_estimated);
    Serial.printf("\nANOMALY STATE : %s (Mahalanobis D2=%.3f, baseline self-calibrated)", result.status_label, result.mahalanobis_D2);
    Serial.printf("\n------------------- DATA MENTAH SENSOR -----------------");
    Serial.printf("\nGETARAN (RMS) : %7.4f", merged.rms_getaran);
    Serial.printf("\nSUARA (RMS)   : %7.2f", merged.rms_suara);
    Serial.printf("\nARUS MOTOR    : %7.4f A", merged.arus);
    Serial.printf("\nSUHU OPERASI  : %7.2f C", merged.suhu);
    Serial.printf("\n========================================================\n");
#endif

    vTaskDelay(pdMS_TO_TICKS(TICK_DELAY_REPORT));
}
