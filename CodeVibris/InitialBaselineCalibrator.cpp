//"InitialBaselineCalibrator.cpp
#include "InitialBaselineCalibrator.h"
#include "CovarianceMatrixSolver.h"
#include <Preferences.h>
#include <Arduino.h>

// 60 detik kalibrasi @ ~1 sample/detik (mengikuti TICK_DELAY_REPORT di main.ino)
// dengan margin ekstra kalau caller manggil lebih sering dari itu.
#define CALIBRATION_MAX_SAMPLES 120

bool addBandEnergyCalibrationSample(float bandEnergies[4]);
void computeBandEnergyBaseline(float meanOutput[4], float stdOutput[4]);
void saveBandBaselineToFlash(float mean[4], float std[4]);
bool loadBandBaselineFromFlash(float meanOutput[4], float stdOutput[4]);
static float calibrationBuffer[CALIBRATION_MAX_SAMPLES][4];
static int   calibrationSampleCount = 0;
static bool  calibrationActive = false;

static Preferences flashStorage;
static const char* NVS_NAMESPACE = "baseline";
static bool lastCalibrationValid = false;
bool isLastCalibrationValid() {
    return lastCalibrationValid;
}
void startCalibrationPhase() {
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

    for (int f = 0; f < 4; f++) {
        double sum = 0.0;
        for (int i = 0; i < calibrationSampleCount; i++) sum += calibrationBuffer[i][f];
        meanOutput[f] = (float)(sum / calibrationSampleCount);
    }

    float rawCovariance[4][4];
    for (int a = 0; a < 4; a++) {
        for (int b = 0; b < 4; b++) {
            double sum = 0.0;
            for (int i = 0; i < calibrationSampleCount; i++) {
                sum += (double)(calibrationBuffer[i][a] - meanOutput[a]) *
                       (double)(calibrationBuffer[i][b] - meanOutput[b]);
            }
            rawCovariance[a][b] = (float)(sum / (calibrationSampleCount - 1));
        }
    }

    // GUARD BARU: tolak baseline kalau ada fitur dengan variance mendekati nol.
    // Ini kondisi fisik "device tidak melihat aktivitas nyata" (motor mati/diam),
    // bukan cuma masalah numerik — kalibrasi HARUS diulang dengan mesin aktif.
    const char* featureNames[4] = {"Getaran", "Suara", "Arus", "Suhu"};
    const float MIN_ACCEPTABLE_VARIANCE = 1e-4f;
    bool calibrationValid = true;

    for (int f = 0; f < 4; f++) {
        if (rawCovariance[f][f] < MIN_ACCEPTABLE_VARIANCE) {
            Serial.printf("[Calibrator] ERROR: variance %s = %.8f, terlalu rendah. "
                          "Mesin kemungkinan TIDAK AKTIF saat kalibrasi. Kalibrasi DITOLAK.\n",
                          featureNames[f], rawCovariance[f][f]);
            calibrationValid = false;
        }
    }
    
    lastCalibrationValid = calibrationValid;
    if (!calibrationValid) {
        // Jangan hasilkan baseline sama sekali — biarkan caller tahu harus ulang.
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
