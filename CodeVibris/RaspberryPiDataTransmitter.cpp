// RaspberryPiDataTransmitter.cpp
#include "RaspberryPiDataTransmitter.h"
#include <Arduino.h>

void Transmitter_Init(long baudRate) {
    // Tidak perlu Serial1.begin() — Serial (USB) sudah di-init di setup() main.ino
    // dengan baudRate yang sama (115200). Fungsi ini sengaja jadi no-op,
    // dipertahankan agar kontrak fungsi tetap konsisten kalau nanti pindah
    // ke UART1/GPIO terpisah (produk final).
    (void)baudRate;
    Serial.println(F("[Transmitter] Mode USB — data dikirim lewat port Serial yang sama dengan debug."));
}

void Transmitter_SendResult(SensorFeatures features, DetectionResult result) {
    // Pakai Serial (USB), BUKAN Serial1 — karena kabel fisiknya USB-C ke USB-A,
    // bukan GPIO UART1 terpisah. Baris ini akan campur dengan baris debug lain
    // di stream yang sama — itu sudah diantisipasi di parser Python (skip baris non-JSON).
    Serial.printf(
        "{\"rms_v\":%.4f,\"rms_a\":%.2f,\"cur\":%.4f,\"temp\":%.2f,"
        "\"rpm\":%.2f,\"d2\":%.2f,\"status\":\"%s\"}\n",
        features.rms_getaran,
        features.rms_suara,
        features.arus,
        features.suhu,
        result.rpm_estimated,
        result.mahalanobis_D2,
        result.status_label
    );
}
