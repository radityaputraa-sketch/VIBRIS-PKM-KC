//AdaptiveBaselineLearner.cpp
#include "AdaptiveBaselineLearner.h"
#include "CovarianceMatrixSolver.h"
#include <Arduino.h>
#include <math.h>

// Learning rate EMA  kecil supaya baseline bergerak pelan, tidak reaktif
// terhadap noise sesaat (sesuai prinsip di header: perubahan wajar bearing
// aus dipelajari bertahap, bukan tiba-tiba).
#define BASELINE_LEARNING_RATE 0.01f

// Invers matriks itu operasi berat — tidak dihitung ulang tiap update,
// cuma tiap N sample. Sesuai catatan performa di header aslinya.
#define INVERSE_RECOMPUTE_INTERVAL 20

static float currentMean[4];
static float currentVar[4];           // BARU: variance per fitur, EMA -- gantikan featureStdDev statis
static float currentRawSigma[4][4];   // kovarians di RUANG STANDARDISASI (pakai currentVar saat itu)
static float currentSigmaInverse[4][4];
static int   updatesSinceLastInverse = 0;
static bool  learnerInitialized = false;

bool isBaselineLearnerReady() {
    return learnerInitialized;
}

void initializeBaselineLearner(float initialMean[4], float initialStd[4], float initialSigmaInverse[4][4]) {
    for (int i = 0; i < 4; i++) {
        currentMean[i] = initialMean[i];
        float sd = initialStd[i];
        currentVar[i] = sd * sd;
        if (currentVar[i] < 1e-8f) currentVar[i] = 1e-8f;
    }

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            currentRawSigma[i][j] = initialSigmaInverse[i][j];

    // Invers pertama dihitung langsung di sini, supaya getCurrentBaseline()
    // bisa langsung dipanggil tanpa perlu nunggu update pertama masuk dulu.
    solveMatrixInverse4x4(initialSigmaInverse, currentSigmaInverse);

    updatesSinceLastInverse = 0;
    learnerInitialized = true;
    Serial.println(F("[AdaptiveLearner] Baseline learner diinisialisasi (mean + std-dev + sigma, semuanya adaptif)."));
}

void updateBaselineIfNormal(float currentRawFeatures[4], bool isCurrentStatusNormal) {
    if (!learnerInitialized) {
        Serial.println(F("[AdaptiveLearner] ERROR: belum diinisialisasi, update diabaikan."));
        return;
    }

    // GUARD PALING PENTING DI MODUL INI: kalau status bukan Normal, jangan
    // update baseline sama sekali.
    if (!isCurrentStatusNormal) {
        return;
    }

    // 1. EMA mean, di RUANG RAW (satuan asli: m/s2, Ampere, dst) --
    // supaya currentMean tetap bermakna fisik, bukan angka standar.
    for (int i = 0; i < 4; i++) {
        currentMean[i] += BASELINE_LEARNING_RATE * (currentRawFeatures[i] - currentMean[i]);
    }

    // 2. BARU: EMA variance per fitur, JUGA di ruang RAW. Ini yang dulu
    // tidak ada -- std-dev dulu statis dari kalibrasi 180 detik pertama,
    // sekarang ikut bergerak pelan konsisten dengan mean & kovarians.
    float rawDiff[4];
    for (int i = 0; i < 4; i++) {
        rawDiff[i] = currentRawFeatures[i] - currentMean[i];
        float instantVar = rawDiff[i] * rawDiff[i];
        currentVar[i] += BASELINE_LEARNING_RATE * (instantVar - currentVar[i]);
        if (currentVar[i] < 1e-8f) currentVar[i] = 1e-8f;  // proteksi div-by-zero
    }

    // 3. Standardisasi pakai std-dev TERKINI (bukan statis dari luar) --
    // baru dipakai untuk update kovarians. Ini yang membuat langkah 2 & 3
    // konsisten satu sama lain, tidak seperti versi lama yang
    // standardisasinya pakai angka beku sementara mean/sigma terus bergerak.
    float stdDiff[4];
    for (int i = 0; i < 4; i++) {
        float sd = sqrtf(currentVar[i]);
        stdDiff[i] = rawDiff[i] / sd;
    }

    for (int a = 0; a < 4; a++) {
        for (int b = 0; b < 4; b++) {
            float instantCov = stdDiff[a] * stdDiff[b];
            currentRawSigma[a][b] += BASELINE_LEARNING_RATE * (instantCov - currentRawSigma[a][b]);
        }
    }

    updatesSinceLastInverse++;

    // Invers cuma dihitung ulang tiap N update, bukan tiap siklus, karena
    // operasi Gauss-Jordan 4x4 tergolong berat untuk dipanggil di setiap
    // siklus deteksi real-time.
    if (updatesSinceLastInverse >= INVERSE_RECOMPUTE_INTERVAL) {
        applyShrinkageRegularization(currentRawSigma);
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

// BARU
void getCurrentStdDev(float stdOutput[4]) {
    for (int i = 0; i < 4; i++) stdOutput[i] = sqrtf(currentVar[i]);
}