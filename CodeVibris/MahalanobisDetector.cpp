#include "MahalanobisDetector.h"
#include "MultiSensorFeatureMerger.h"
#include "AdaptiveBaselineLearner.h"
#include "CovarianceMatrixSolver.h"
#include "DualCoreTaskScheduler.h"
#include <Arduino.h>
#include <string.h>

// Nilai kritis chi-square, df=4 (4 fitur sensor), sesuai dokumentasi header:
// 95% confidence = 9.49, 99% confidence = 13.28. Standar statistik, bukan tebakan.
#define CHI_SQUARE_95 9.49f
#define CHI_SQUARE_99 13.28f

const char* classifyStatusFromD2(float d2Value) {
    if (d2Value <= CHI_SQUARE_95) return "Normal";
    if (d2Value <= CHI_SQUARE_99) return "Waspada";
    return "Bahaya";
}

DetectionResult runDetectionCycle() {
    DetectionResult result;
    result.rpm_estimated = 0.0f;
    result.mahalanobis_D2 = 0.0f;
    strncpy(result.status_label, "Unknown", sizeof(result.status_label) - 1);

    // GUARD 1: kalibrasi belum dijalankan (misal device baru boot, belum
    // ada baseline dari flash maupun kalibrasi manual). Tanpa guard ini,
    // baseline kosong (mean=0, Sigma^-1=identity default) akan menghasilkan
    // D^2 yang salah total — bukan error, tapi angka menyesatkan yang diam-diam salah.
    if (!isBaselineLearnerReady()) {
        strncpy(result.status_label, "NotCalibrated", sizeof(result.status_label) - 1);
        return result;
    }

    SensorFeatures merged;
    bool fresh = getMergedFeatures(&merged);

    // GUARD 2: data sensor basi/tidak lengkap. Menghitung D^2 dari data
    // stale bisa salah klasifikasi — sensor mati kadang justru terbaca
    // "diam", mirip kondisi normal, padahal itu fault, bukan Normal sungguhan.
    if (!fresh) {
        strncpy(result.status_label, "SensorFault", sizeof(result.status_label) - 1);
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
    const char* label = classifyStatusFromD2(d2);
    bool isNormal = (strcmp(label, "Normal") == 0);

    // Baseline hanya diperbarui kalau status Normal — guard ini yang
    // membuat self-baseline learning aman (lihat header AdaptiveBaselineLearner).
    updateBaselineIfNormal(currentFeatures, isNormal);

    result.rpm_estimated = Scheduler_GetLatestRPM();
    result.mahalanobis_D2 = d2;
    strncpy(result.status_label, label, sizeof(result.status_label) - 1);

    return result;
}
