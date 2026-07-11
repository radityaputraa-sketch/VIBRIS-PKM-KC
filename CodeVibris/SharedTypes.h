// SharedTypes.h
#pragma once

#include <stdint.h>
#include "config.h" // Mengunci FFT_SAMPLES agar sinkron secara arsitektur
enum FeatureIndex { FEAT_VIBRATION = 0, FEAT_AUDIO = 1, FEAT_CURRENT = 2, FEAT_TEMP = 3, FEAT_COUNT = 4 };
// ===================================================================
// 1. BUFFER DATA MENTAH (RAW DATA BUFFER - CORE 0 TO DSP)
// ===================================================================
struct VibrationBuffer {
    float samples[FFT_SAMPLES]; // Array float untuk menampung sampel getaran LIS3DH [cite: 2026-05-06]
    uint32_t timestamp;         // Waktu pengambilan sampel (millis/micros)
};

// ===================================================================
// 2. WADAH FITUR SENSOR (FEATURE EXTRACTION - INTER-CORE DATA PASSING)
// ===================================================================
struct SensorFeatures {
    volatile float rms_getaran; // Hasil kalkulasi Root Mean Square dari LIS3DH [cite: 2026-05-06]
    volatile float rms_suara;   // Hasil konversi amplitudo/daya dari INMP441 [cite: 2026-05-06]
    volatile float arus;        // Hasil kalkulasi RMS dari sensor SCT [cite: 2026-04-10]
    volatile float suhu;        // Hasil pembacaan dari sensor suhu DS18H [cite: 2026-04-10]
    volatile bool valid;        // Flag integritas data untuk error handling / fail-safe mechanism
};

// ===================================================================
// 3. HASIL KEPUTUSAN DIAGNOSTIK (INFERENCE RESULT)
// ===================================================================
struct DetectionResult {
    float rpm_estimated;        // Estimasi kecepatan putar mesin hasil analisis spektrum
    float mahalanobis_D2;       // Nilai Jarak Mahalanobis untuk deteksi anomali [cite: 2026-05-06]
    char status_label[16];      // String status: "Normal", "Waspada", atau "Bahaya"
};
