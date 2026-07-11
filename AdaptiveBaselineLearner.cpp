//AdaptiveBaselineLearner.cpp
#include "AdaptiveBaselineLearner.h"
#include "CovarianceMatrixSolver.h"
#include <Arduino.h>

// Learning rate EMA — kecil supaya baseline bergerak pelan, tidak reaktif
// terhadap noise sesaat (sesuai prinsip di header: perubahan wajar bearing
// aus dipelajari bertahap, bukan tiba-tiba).
#define BASELINE_LEARNING_RATE 0.01f

// Invers matriks itu operasi berat — tidak dihitung ulang tiap update,
// cuma tiap N sample. Sesuai catatan performa di header aslinya.
#define INVERSE_RECOMPUTE_INTERVAL 20

static float currentMean[4];
static float currentRawSigma[4][4];
static float currentSigmaInverse[4][4];
static int   updatesSinceLastInverse = 0;
static bool  learnerInitialized = false;

bool isBaselineLearnerReady() {
    return learnerInitialized;
}
void initializeBaselineLearner(float initialMean[4], float initialSigma[4][4]) {
    for (int i = 0; i < 4; i++) currentMean[i] = initialMean[i];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            currentRawSigma[i][j] = initialSigma[i][j];

    // Invers pertama dihitung langsung di sini, supaya getCurrentBaseline()
    // bisa langsung dipanggil tanpa perlu nunggu update pertama masuk dulu.
    solveMatrixInverse4x4(currentRawSigma, currentSigmaInverse);

    updatesSinceLastInverse = 0;
    learnerInitialized = true;
    Serial.println(F("[AdaptiveLearner] Baseline learner diinisialisasi."));
}

void updateBaselineIfNormal(float currentFeatures[4], bool isCurrentStatusNormal) {
    if (!learnerInitialized) {
        Serial.println(F("[AdaptiveLearner] ERROR: belum diinisialisasi, update diabaikan."));
        return;
    }

    // GUARD PALING PENTING DI MODUL INI: kalau status bukan Normal, jangan
    // update baseline sama sekali. Kerusakan yang berkembang bertahap tidak
    // boleh ikut "dipelajari" jadi kondisi normal baru — itu akan membuat
    // sistem makin tidak sensitif ke masalah yang justru sedang terjadi.
    if (!isCurrentStatusNormal) {
        return;
    }

    // EMA update — mean bergerak sedikit demi sedikit ke arah data terbaru
    for (int i = 0; i < 4; i++) {
        currentMean[i] += BASELINE_LEARNING_RATE * (currentFeatures[i] - currentMean[i]);
    }

    // EMA update untuk covariance: pakai deviasi dari mean yang BARU saja
    // diperbarui di atas, supaya konsisten satu siklus yang sama.
    float diff[4];
    for (int i = 0; i < 4; i++) diff[i] = currentFeatures[i] - currentMean[i];

    for (int a = 0; a < 4; a++) {
        for (int b = 0; b < 4; b++) {
            float instantCov = diff[a] * diff[b];
            currentRawSigma[a][b] += BASELINE_LEARNING_RATE * (instantCov - currentRawSigma[a][b]);
        }
    }

    updatesSinceLastInverse++;

    // Invers cuma dihitung ulang tiap N update, bukan tiap siklus, karena
    // operasi Gauss-Jordan 4x4 tergolong berat untuk dipanggil di setiap
    // siklus deteksi real-time.
    if (updatesSinceLastInverse >= INVERSE_RECOMPUTE_INTERVAL) {
        solveMatrixInverse4x4(currentRawSigma, currentSigmaInverse);
        updatesSinceLastInverse = 0;
    }
}

void getCurrentBaseline(float meanOutput[4], float sigmaInverseOutput[4][4]) {
    for (int i = 0; i < 4; i++) meanOutput[i] = currentMean[i];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            sigmaInverseOutput[i][j] = currentSigmaInverse[i][j];
}