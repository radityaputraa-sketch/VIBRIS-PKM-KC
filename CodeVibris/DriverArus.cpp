// DriverArus.cpp
// ===================================================================
// CATATAN DERIVASI ARUS_CAL_FACTOR (WAJIB UPDATE SESUAI HARDWARE):
//
// Rumus: CAL_FACTOR = (CT_RATIO / BURDEN_OHM) / ADC_MAX
//
// Contoh untuk SCT-30A:
//   CT Ratio    : 1800:1 (cek datasheet modul spesifikmu)
//   Burden R    : 62Ω (nilai burden resistor yang terpasang)
//   ADC Range   : 4095 (12-bit)
//   Faktor      : (1800 / 62) / 4095 = 0.00709
//
// CARA KALIBRASI EMPIRIS (lebih akurat):
//   1. Clamp SCT ke kabel motor yang dialiri arus diketahui
//      (ukur pakai tang ampere sebagai referensi)
//   2. Print rmsADC ke Serial tanpa dikali CAL_FACTOR
//   3. CAL_FACTOR = arus_referensi_ampere / rmsADC_terbaca
//
// PERBARUI nilai ini setelah kalibrasi fisik, jangan pakai angka tebakan.
// ===================================================================

#include "DriverArus.h"
#include "config.h"
#include <math.h>

// Jumlah sample harus kelipatan bulat SATU SIKLUS AC penuh.
// Di 50Hz dengan sampling interval 100µs (10kHz):
//   1 siklus = 10000/50 = 200 sample
//   3 siklus = 600 sample (dipilih: margin lebih lebar, noise statistik lebih kecil)
// JANGAN pakai angka non-kelipatan (misal 500) → RMS berfluktuasi karena
// setengah siklus "menggantung" di akhir window.
#define ARUS_RMS_SAMPLE_COUNT   600   // override ARUS_SAMPLE_COUNT di config.h khusus file ini
#include "MultiSensorFeatureMerger.h"
// Jumlah sample untuk fase warm-up HPF (filter harus converge ke baseline
// sebelum RMS mulai dihitung — spike transient awal dibuang di sini)
#define ARUS_WARMUP_SAMPLES     200

void TaskDriverArus(void *pvParameters) {
    (void)pvParameters;

    // FIX 1: Set attenuation 11dB SEBELUM analogRead pertama.
    // Tanpa ini ESP32-S3 default 0dB → hanya baca 0–0.75V → ADC selalu clipping
    // karena sinyal SCT yang di-bias ke 1.65V tidak terbaca sama sekali.
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_SCT_ADC, ADC_11db); // range penuh 0–3.3V

    // FIX 2: Inisialisasi filter state dengan nilai ADC mid-point NYATA,
    // bukan asumsi 2048. Baca dulu beberapa sample untuk dapat estimasi DC.
    // Ini mencegah spike transient besar di iterasi pertama HPF.
    float dcEstimate = 0.0f;
    for (int i = 0; i < 32; i++) {
        dcEstimate += (float)analogRead(PIN_SCT_ADC);
        delayMicroseconds(100);
    }
    dcEstimate /= 32.0f;

    float prevRawSample      = dcEstimate; // inisialisasi ke DC aktual, bukan asumsi 2048
    float prevFilteredSample = 0.0f;
    const float alpha = 0.996f;
    // Penjelasan alpha=0.996 pada 10kHz:
    //   dt = 100µs, RC = alpha*dt/(1-alpha) = 0.996*0.0001/0.004 = 0.0249s
    //   fc = 1/(2π*RC) = 6.4Hz → buang DC dan drift lambat, lewatkan 50Hz sinyal arus

    // FIX 3: Warm-up phase — jalankan HPF dulu tanpa akumulasi RMS
    // sampai filter converge ke baseline yang benar.
    // Tanpa ini, spike awal akibat initial condition akan masuk ke sumSquared
    // dan menghasilkan RMS palsu yang terlalu tinggi di window pertama.
    for (int i = 0; i < ARUS_WARMUP_SAMPLES; i++) {
        float rawSample = (float)analogRead(PIN_SCT_ADC);
        float filtered  = alpha * (prevFilteredSample + rawSample - prevRawSample);
        prevRawSample      = rawSample;
        prevFilteredSample = filtered;
        delayMicroseconds(100);
    }

    for (;;) {
        // FIX 4: Gunakan 600 sample (3 siklus penuh @50Hz pada 10kHz sampling).
        // Sebelumnya 500 sample = 2.5 siklus → RMS berfluktuasi ±beberapa persen
        // tergantung fase awal sampling, bukan karena noise nyata.
        double sumSquared = 0.0;

        for (int i = 0; i < ARUS_RMS_SAMPLE_COUNT; i++) {
            float rawSample = (float)analogRead(PIN_SCT_ADC);

            // High-pass filter: buang komponen DC (bias tegangan) dan drift lambat.
            // Yang tersisa hanya komponen AC sinusoidal dari arus motor.
            float filteredSample = alpha * (prevFilteredSample + rawSample - prevRawSample);
            prevRawSample        = rawSample;
            prevFilteredSample   = filteredSample;

            sumSquared += (double)(filteredSample * filteredSample);

            // FIX 5: delayMicroseconds adalah busy-wait (tidak yield ke FreeRTOS scheduler).
            // Ini tidak ideal di FreeRTOS, tapi tidak ada alternatif bersih untuk
            // interval <1ms tanpa hardware timer terpisah.
            // Konsekuensi: Core 0 tidak bisa task-switch selama ~60ms window sampling ini.
            // Pertimbangkan pindah ke Core 1 bersama Suhu jika Core 0 mulai overload.
            delayMicroseconds(100); // 100µs → sample rate 10kHz
        }

        float meanSquare      = (float)(sumSquared / ARUS_RMS_SAMPLE_COUNT);
        float rmsADC          = sqrtf(meanSquare);

        // FIX 5: Dokumentasi derivasi konversi
        // rmsADC adalah nilai RMS dalam satuan ADC count (bukan Volt, bukan Ampere).
        // ARUS_CAL_FACTOR mengkonversi langsung ke Ampere.
        // PERBARUI nilai ini berdasarkan kalibrasi empiris (lihat catatan di atas).
        float calculatedCurrent = rmsADC * ARUS_CAL_FACTOR;

        if (calculatedCurrent < ARUS_NOISE_GATE) {
            calculatedCurrent = 0.0f;
        }

        updateCurrentFeature(calculatedCurrent);

        vTaskDelay(pdMS_TO_TICKS(TICK_DELAY_ARUS));
    }
}
