#include "MahalanobisDetector.h"
#include "MultiSensorFeatureMerger.h"
#include "AdaptiveBaselineLearner.h"
#include "CovarianceMatrixSolver.h"
#include "DualCoreTaskScheduler.h"
#include "DiagnosisClassifier.h"
#include <Arduino.h>
#include <string.h>

// Nilai kritis chi-square, df=4 (4 fitur sensor), sesuai dokumentasi header:
// 95% confidence = 9.49, 99% confidence = 13.28. Standar statistik, bukan tebakan.
#define CHI_SQUARE_95 9.49f
#define CHI_SQUARE_99 13.277f

// Baseline energi per-band dipakai DiagnosisClassifier (lihat header:
// modul ini menjawab APAKAH menyimpang, DiagnosisClassifier menjawab DI
// BAGIAN MANA). Disuplai dari luar lewat setDiagnosisBandBaseline() --
// modul ini sendiri tidak tahu cara mengkalibrasi, hanya memakainya.
static float diagBandMean[4]  = {0.0f, 0.0f, 0.0f, 0.0f};
static float diagBandStd[4]   = {1.0f, 1.0f, 1.0f, 1.0f};
static bool  diagBaselineReady = false;

void setDiagnosisBandBaseline(float bandMean[4], float bandStd[4]) {
    for (int i = 0; i < 4; i++) {
        diagBandMean[i] = bandMean[i];
        diagBandStd[i]  = bandStd[i];
    }
    diagBaselineReady = true;
}

const char* classifyStatusFromD2(float d2Value) {
    if (d2Value <= CHI_SQUARE_95) return "Normal";
    if (d2Value <= CHI_SQUARE_99) return "Waspada";
    return "Bahaya";
}

DetectionResult runDetectionCycle() {
    DetectionResult result;
    static unsigned long baselineReadyTimestamp = 0;
    static bool wasReady = false;
    result.rpm_estimated = 0.0f;
    result.mahalanobis_D2 = 0.0f;
    result.diagnosis_confidence = 0.0f;
    strncpy(result.status_label, "Unknown", sizeof(result.status_label) - 1);
    result.status_label[sizeof(result.status_label) - 1] = '\0';
    strncpy(result.diagnosis_label, "N/A", sizeof(result.diagnosis_label) - 1);
    result.diagnosis_label[sizeof(result.diagnosis_label) - 1] = '\0';

    // GUARD 1: kalibrasi belum dijalankan (misal device baru boot, belum
    // ada baseline dari flash maupun kalibrasi manual). Tanpa guard ini,
    // baseline kosong (mean=0, Sigma^-1=identity default) akan menghasilkan
    // D^2 yang salah total — bukan error, tapi angka menyesatkan yang diam-diam salah.
    if (!isBaselineLearnerReady()) {
        strncpy(result.status_label, "NotCalibrated", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
        return result;
    }
    if (!wasReady) {
        baselineReadyTimestamp = millis();
        wasReady = true;
    }

    SensorFeatures merged;
    bool fresh = getMergedFeatures(&merged);

    // GUARD 2: data sensor basi/tidak lengkap.
    if (!fresh) {
        strncpy(result.status_label, "SensorFault", sizeof(result.status_label) - 1);
        result.status_label[sizeof(result.status_label) - 1] = '\0';
        return result;
    }

    float currentFeatures[4] = {
        merged.rms_getaran,
        merged.rms_suara,
        merged.arus,
        merged.suhu
    };

    float baselineMean[4];
    float sigmaInverse[4][4];
    getCurrentBaseline(baselineMean, sigmaInverse);

    float d2 = computeMahalanobisQuadraticForm(currentFeatures, baselineMean, sigmaInverse);
    const unsigned long GRACE_PERIOD_MS = 120000;
    float thresholdMultiplier = 1.0f;
    if (millis() - baselineReadyTimestamp < GRACE_PERIOD_MS) {
        thresholdMultiplier = 1.5f;
    }
    const char* label = classifyStatusFromD2(d2 / thresholdMultiplier);
    
    bool isNormal = (strcmp(label, "Normal") == 0);

    updateBaselineIfNormal(currentFeatures, isNormal);

    result.rpm_estimated = Scheduler_GetLatestRPM();
    result.mahalanobis_D2 = d2;
    strncpy(result.status_label, label, sizeof(result.status_label) - 1);
    result.status_label[sizeof(result.status_label) - 1] = '\0';

    // LAPISAN LANJUTAN: begitu D^2 dihitung, tanya DiagnosisClassifier:
    // menyimpang di band frekuensi MANA (Unbalance/Misalignment/BPFO/BPFI).
    // FIX: kalau RPM tidak reliable (SNR rendah/motor mati), FFTProcessor
    // menge-nol-kan bandEnergies -- dan itu SELALU dibaca "NORMAL" oleh
    // Diagnosis_Classify (z-score energi 0 vs baseline malah negatif, di
    // bawah ambang). Data lapangan buktikan ini: 766 dari 1020 baris status
    // "Bahaya" punya diagnosis "NORMAL" bersamaan (rpm=0). Guard di bawah
    // mencegah label yang diam-diam salah -- kalau sinyal tidak reliable,
    // diagnosis tetap "N/A" (default), bukan "NORMAL" yang menyesatkan.
    if (diagBaselineReady && result.rpm_estimated > 0.0f) {
        float bandEnergies[4];
        Scheduler_GetLatestBandEnergies(bandEnergies);

        char diagLabel[20];
        float diagConfidence = 0.0f;
        Diagnosis_Classify(bandEnergies, diagBandMean, diagBandStd, diagLabel, &diagConfidence);

        strncpy(result.diagnosis_label, diagLabel, sizeof(result.diagnosis_label) - 1);
        result.diagnosis_label[sizeof(result.diagnosis_label) - 1] = '\0';
        result.diagnosis_confidence = diagConfidence;
    }

    return result;
}