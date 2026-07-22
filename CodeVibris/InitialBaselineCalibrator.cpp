//"InitialBaselineCalibrator.cpp
#include "InitialBaselineCalibrator.h"
#include "CovarianceMatrixSolver.h"
#include "InitialBaselineCalibrator.h"
#include "CovarianceMatrixSolver.h"
#include <Preferences.h>
#include <Arduino.h>
#include <Preferences.h>
#include <Arduino.h>


// FIX: kalibrasi sekarang digerbang WAKTU (180 detik nyata via millis() di
// main.ino), bukan jumlah sample -- karena rate loop() terbukti TIDAK
// konstan (data lapangan: 1-5 sample/detik). Buffer diperbesar supaya
// cukup menampung sample terbanyak yang mungkin masuk dalam 180 detik,
// bahkan setelah fix DriverArus.cpp bikin rate naik mendekati 10/detik.
#define CALIBRATION_MAX_SAMPLES 2000

bool addBandEnergyCalibrationSample(float bandEnergies[4]);
void computeBandEnergyBaseline(float meanOutput[4], float stdOutput[4]);
void saveBandBaselineToFlash(float mean[4], float std[4]);
bool loadBandBaselineFromFlash(float meanOutput[4], float stdOutput[4]);

static float calibrationBuffer[CALIBRATION_MAX_SAMPLES][4];
static int   calibrationSampleCount = 0;
static bool  calibrationActive = false;
static float featureStdDev[4] = {1.0f, 1.0f, 1.0f, 1.0f};

static Preferences flashStorage;
static const char* NVS_NAMESPACE = "baseline";
static bool lastCalibrationValid = false;

bool isLastCalibrationValid() {
    return lastCalibrationValid;
}

void startCalibrationPhase() {
    lastCalibrationValid = false;
    calibrationSampleCount = 0;
    calibrationActive = true;
    Serial.println(F("[Calibrator] Fase kalibrasi dimulai — pastikan mesin dalam kondisi NORMAL."));
}

static float bandCalibrationBuffer[CALIBRATION_MAX_SAMPLES][4];
static int   bandCalibrationSampleCount = 0;

bool addBandEnergyCalibrationSample(float bandEnergies[4]) {
    if (!calibrationActive) return false;
    if (bandCalibrationSampleCount >= CALIBRATION_MAX_SAMPLES) return false;

    for (int i = 0; i < 4; i++) {
        bandCalibrationBuffer[bandCalibrationSampleCount][i] = bandEnergies[i];
    }
    bandCalibrationSampleCount++;
    return true;
}

void computeBandEnergyBaseline(float meanOutput[4], float stdOutput[4]) {
    if (bandCalibrationSampleCount < 2) {
        Serial.println(F("[Calibrator] ERROR: sample band energy terlalu sedikit."));
        for (int i = 0; i < 4; i++) { meanOutput[i] = 0.2f; stdOutput[i] = 0.1f; } // fallback aman, bukan 0
        return;
    }
    for (int f = 0; f < 4; f++) {
        double sum = 0.0;
        for (int i = 0; i < bandCalibrationSampleCount; i++) sum += bandCalibrationBuffer[i][f];
        meanOutput[f] = (float)(sum / bandCalibrationSampleCount);
    }
    for (int f = 0; f < 4; f++) {
        double sumSqDiff = 0.0;
        for (int i = 0; i < bandCalibrationSampleCount; i++) {
            double d = bandCalibrationBuffer[i][f] - meanOutput[f];
            sumSqDiff += d * d;
        }
        stdOutput[f] = (float)sqrt(sumSqDiff / (bandCalibrationSampleCount - 1));
    }
    Serial.printf("[Calibrator] Band energy baseline selesai dari %d sample.\n", bandCalibrationSampleCount);
}

void saveBandBaselineToFlash(float mean[4], float std[4]) {
    flashStorage.begin("baseline_band", false);
    flashStorage.putBytes("mean", mean, sizeof(float) * 4);
    flashStorage.putBytes("std", std, sizeof(float) * 4);
    flashStorage.end();
}

bool loadBandBaselineFromFlash(float meanOutput[4], float stdOutput[4]) {
    flashStorage.begin("baseline_band", true);
    size_t meanLen = flashStorage.getBytesLength("mean");
    size_t stdLen  = flashStorage.getBytesLength("std");
    if (meanLen != sizeof(float)*4 || stdLen != sizeof(float)*4) {
        flashStorage.end();
        return false;
    }
    flashStorage.getBytes("mean", meanOutput, meanLen);
    flashStorage.getBytes("std", stdOutput, stdLen);
    flashStorage.end();
    return true;
}

bool addCalibrationSample(SensorFeatures sample) {
    if (!calibrationActive) return false;

    // GUARD PENTING: tolak sample yang stale/tidak valid. Kalibrasi yang
    // memasukkan data basi (misal salah satu sensor putus sesaat) akan
    // merusak mean & covariance baseline secara permanen — lebih baik
    // skip satu sample daripada baseline tercemar data buruk.
    if (!sample.valid) {
        Serial.println(F("[Calibrator] Sample ditolak: data tidak valid/stale."));
        return false;
    }

    if (calibrationSampleCount >= CALIBRATION_MAX_SAMPLES) {
        calibrationActive = false; // buffer penuh, fase otomatis selesai
        Serial.println(F("[Calibrator] Buffer kalibrasi penuh, fase selesai."));
        return false;
    }

    calibrationBuffer[calibrationSampleCount][FEAT_VIBRATION] = sample.rms_getaran;
    calibrationBuffer[calibrationSampleCount][FEAT_AUDIO]     = sample.rms_suara;
    calibrationBuffer[calibrationSampleCount][FEAT_CURRENT]   = sample.arus;
    calibrationBuffer[calibrationSampleCount][FEAT_TEMP]      = sample.suhu;
    calibrationSampleCount++;
    return true;
}

void computeInitialBaseline(float meanOutput[4], float sigmaInverseOutput[4][4]) {
    if (calibrationSampleCount < 2) {
        Serial.println(F("[Calibrator] ERROR: sample terlalu sedikit untuk hitung baseline."));
        for (int i = 0; i < 4; i++) meanOutput[i] = 0.0f;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                sigmaInverseOutput[i][j] = (i == j) ? 1.0f : 0.0f;
        return;
    }

    // ===== 1. HITUNG MEAN (tidak berubah dari versi asli) =====
    for (int f = 0; f < 4; f++) {
        double sum = 0.0;
        for (int i = 0; i < calibrationSampleCount; i++) sum += calibrationBuffer[i][f];
        meanOutput[f] = (float)(sum / calibrationSampleCount);
    }

    // ===== 2. HITUNG STD PER FITUR, LALU STANDARDISASI SETIAP SAMPLE =====
    // Ini akar perbaikan: tanpa langkah ini, fitur dengan skala besar (suhu,
    // suara) akan mendominasi matriks kovarians dibanding fitur skala kecil
    // (arus), membuat matriks ill-conditioned secara struktural.
    for (int f = 0; f < 4; f++) {
        double sumSqDiff = 0.0;
        for (int i = 0; i < calibrationSampleCount; i++) {
            double d = calibrationBuffer[i][f] - meanOutput[f];
            sumSqDiff += d * d;
        }
        featureStdDev[f] = (float)sqrt(sumSqDiff / (calibrationSampleCount - 1));
        if (featureStdDev[f] < 1e-4f) featureStdDev[f] = 1e-4f; // proteksi div-by-zero murni numerik
    }

    static double standardizedBuffer[CALIBRATION_MAX_SAMPLES][4];
    for (int i = 0; i < calibrationSampleCount; i++)
        for (int f = 0; f < 4; f++)
            standardizedBuffer[i][f] = (calibrationBuffer[i][f] - meanOutput[f]) / featureStdDev[f];

    // ===== 3. HITUNG KOVARIANS DARI DATA YANG SUDAH DISTANDARDISASI =====
    // Sudah mean-centered oleh standardisasi di atas, jadi tidak perlu
    // dikurangi mean lagi di sini.
    float rawCovariance[4][4];
    for (int a = 0; a < 4; a++) {
        for (int b = 0; b < 4; b++) {
            double sum = 0.0;
            for (int i = 0; i < calibrationSampleCount; i++) {
                sum += standardizedBuffer[i][a] * standardizedBuffer[i][b];
            }
            rawCovariance[a][b] = (float)(sum / (calibrationSampleCount - 1));
        }
    } // <-- loop 'a' ditutup TEPAT DI SINI, tidak ada blok lain nyangkut di dalamnya

    // CATATAN: blok VARIANCE_FLOOR_RATIO versi lama SENGAJA DIHAPUS di sini.
    // Setelah standardisasi di langkah 2, variansi tiap fitur otomatis ≈1,
    // jadi "floor" berbasis skala mean lama sudah tidak relevan lagi dan
    // berpotensi mendistorsi hasil yang sudah benar secara statistik.

    // GUARD: tolak baseline kalau ada fitur dengan variance mendekati nol.
    // Ini kondisi fisik "device tidak melihat aktivitas nyata" (motor mati/diam),
    // bukan cuma masalah numerik — kalibrasi HARUS diulang dengan mesin aktif.
    const char* featureNames[4] = {"Getaran", "Suara", "Arus", "Suhu"};
    const float MIN_ACCEPTABLE_VARIANCE = 5e-4f;
    bool calibrationValid = true;

    for (int f = 0; f < 4; f++) {
        if (rawCovariance[f][f] < MIN_ACCEPTABLE_VARIANCE) {
            Serial.printf("[Calibrator] ERROR: variance %s = %.8f, terlalu rendah. "
                          "Mesin kemungkinan TIDAK AKTIF saat kalibrasi. Kalibrasi DITOLAK.\n",
                          featureNames[f], rawCovariance[f][f]);
            calibrationValid = false;
        }
    }

    if (!calibrationValid) {
        // Jangan hasilkan baseline sama sekali — biarkan caller tahu harus ulang.
        for (int i = 0; i < 4; i++) meanOutput[i] = 0.0f;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                sigmaInverseOutput[i][j] = (i == j) ? 1.0f : 0.0f;
        return;
    }

    // ===== 4. SHRINKAGE + CEK KONDISI MATRIKS, BARU DI-INVERS =====
    // Urutan wajib: shrinkage dulu (memperbaiki matriks), baru dicek apakah
    // hasilnya sudah sehat, baru diinvers. Kalau dibalik, pengecekan jadi
    // useless karena mengevaluasi matriks yang belum diperbaiki.
    applyShrinkageRegularization(rawCovariance);

    if (!checkMatrixWellConditioned(rawCovariance)) {
        Serial.println(F("[Calibrator] ERROR: matriks kovarians ill-conditioned setelah shrinkage. Kalibrasi DITOLAK."));
        for (int i = 0; i < 4; i++) meanOutput[i] = 0.0f;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                sigmaInverseOutput[i][j] = (i == j) ? 1.0f : 0.0f;
        return;
    }

    solveMatrixInverse4x4(rawCovariance, sigmaInverseOutput);
    lastCalibrationValid = true;
    Serial.printf("[Calibrator] Baseline selesai dari %d sample.\n", calibrationSampleCount);
}

void saveBaselineToFlash(float mean[4], float sigmaInverse[4][4]) {
    flashStorage.begin(NVS_NAMESPACE, false); // false = read-write mode
    flashStorage.putBytes("mean", mean, sizeof(float) * 4);
    flashStorage.putBytes("sigmaInv", sigmaInverse, sizeof(float) * 16);
    flashStorage.end();
    Serial.println(F("[Calibrator] Baseline tersimpan ke flash (NVS)."));
}

bool loadBaselineFromFlash(float meanOutput[4], float sigmaInverseOutput[4][4]) {
    flashStorage.begin(NVS_NAMESPACE, true); // true = read-only mode

    size_t meanLen = flashStorage.getBytesLength("mean");
    size_t sigmaLen = flashStorage.getBytesLength("sigmaInv");

    // Kalau key belum pernah disimpan (device baru pertama kali nyala),
    // panjangnya 0 — jangan dipaksa baca, itu akan mengisi buffer sampah.
    if (meanLen != sizeof(float) * 4 || sigmaLen != sizeof(float) * 16) {
        flashStorage.end();
        Serial.println(F("[Calibrator] Tidak ada baseline tersimpan di flash."));
        return false;
    }

    flashStorage.getBytes("mean", meanOutput, meanLen);
    flashStorage.getBytes("sigmaInv", sigmaInverseOutput, sigmaLen);
    flashStorage.end();

    Serial.println(F("[Calibrator] Baseline berhasil dimuat dari flash."));
    return true;
}

// BARU: getter supaya file lain (MahalanobisDetector.cpp) bisa mengambil
// featureStdDev yang sama persis dipakai saat kalibrasi, untuk menstandardisasi
// fitur real-time dengan skala yang konsisten dengan baseline.
void getFeatureStdDev(float stdDevOutput[4]) {
    for (int i = 0; i < 4; i++) stdDevOutput[i] = featureStdDev[i];
}
