// GenericThresholdClassifier.cpp
#include "GenericThresholdClassifier.h"
#include <string.h>

// ===================================================================
// AMBANG BATAS GENERIK — TIDAK BERGANTUNG PADA KALIBRASI PER-MESIN
// ===================================================================
// Angka-angka ini SENGAJA disamakan dengan ambang batas fallback yang
// sebelumnya sudah dipakai di sisi dashboard Python (lihat fungsi
// _evaluate_diagnosis di dbintegrasi.py), supaya firmware dan dashboard
// selalu konsisten satu sama lain, di mana pun modul sensor dipasang.
//
// Kalau nanti karakteristik mesin uji berbeda jauh (motor lebih besar/kecil,
// dudukan sensor beda, dll.), cukup ubah angka di sini — tidak perlu
// mengulang proses kalibrasi apa pun.
#define VIB_WASPADA   0.18f   // RMS getaran (satuan sesuai keluaran DriverGetaran)
#define VIB_BAHAYA    0.25f
#define TEMP_WASPADA  42.0f   // Celcius
#define TEMP_BAHAYA   50.0f   // Celcius

const char* classifyStatusFixedThreshold(const SensorFeatures &features, float* severityScoreOut) {
    // Skor keparahan = seberapa dekat pembacaan saat ini terhadap ambang
    // batas Bahaya (1.0 = tepat di ambang Bahaya). Dipakai dashboard sebagai
    // pengganti angka Mahalanobis D2 yang dulu perlu baseline.
    float vibScore  = features.rms_getaran / VIB_BAHAYA;
    float tempScore = features.suhu / TEMP_BAHAYA;
    float severity  = (vibScore > tempScore) ? vibScore : tempScore;
    if (severityScoreOut) {
        *severityScoreOut = severity;
    }

    if (features.rms_getaran > VIB_BAHAYA || features.suhu > TEMP_BAHAYA) {
        return "Bahaya";
    }
    if (features.rms_getaran > VIB_WASPADA || features.suhu > TEMP_WASPADA) {
        return "Waspada";
    }
    return "Normal";
}
