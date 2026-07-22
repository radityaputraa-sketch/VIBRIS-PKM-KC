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
    float rms_x_raw;
    float rms_y_raw;
    float rms_z_raw;
    float actual_rate_hz;
};
struct AudioBuffer {
    float samples[AUDIO_FFT_SAMPLES];
    uint32_t timestamp;
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
    char diagnosis_label[20];   // "UNBALANCE" / "MISALIGNMENT" / "BEARING_BPFO" / "BEARING_BPFI" / "NORMAL" / "N/A"
    float diagnosis_confidence; // Z-score band paling menyimpang
    char audio_diagnosis_label[20];      // BARU
    float audio_diagnosis_confidence;
};

// Tbearign abangku
struct BearingSpec {
    int   n_balls;
    float d_ball_mm;
    float D_pitch_mm;
    float phi_deg;
    const char* label;
};

// Tabel bearing umum (approksimasi Pd = rata-rata bore+OD, Bd dari referensi
// umum seri 62xx -- BUKAN data pabrikan presisi. Verifikasi manual pakai
// jangka sorong kalau butuh akurasi tinggi untuk laporan resmi).
static const BearingSpec BEARING_TABLE[] = {
    // n_balls, Bd(mm), Pd(mm), phi(deg), label
//    {8, 5.5f,  21.0f, 0.0f, "6201 (12x32x10)"},
    {8, 6.35f, 25.0f, 0.0f, "6202 (15x35x11)"},   // default motor 1/4HP kamu
//    {8, 6.75f, 28.5f, 0.0f, "6203 (17x40x12)"},
//    {8, 7.6f,  33.5f, 0.0f, "6204 (20x47x14)"},
};
//#define BEARING_TABLE_SIZE (sizeof(BEARING_TABLE)/sizeof(BEARING_TABLE[0]))
//#define BEARING_DEFAULT_INDEX 1   // 6202 -- sesuai motor 1/4HP 1-fasa yang kamu tes

// PENTING: bukan 'static' -- ini DIDEKLARASIKAN di sini, tapi
// DIDEFINISIKAN cuma sekali di FFTProcessor.cpp (lihat FIX 2).
// Supaya semua file (main.ino, FFTProcessor.cpp) pegang variabel YANG SAMA.
extern BearingSpec currentBearingSpec;