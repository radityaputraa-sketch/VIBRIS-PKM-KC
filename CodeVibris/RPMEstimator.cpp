#include "RPMEstimator.h"
#include <math.h>
#include <Arduino.h>
#include <algorithm>

// Batas rentang RPM motor kecil sesuai proposal (300-3000 RPM = 5-50 Hz)
// Di luar rentang ini diabaikan supaya nggak salah tangkap noise frekuensi
// rendah (getaran lingkungan, goyangan meja) atau harmonik tinggi yang
// bukan representasi RPM asli.
#define FR_MIN_HZ 5.0
#define FR_MAX_HZ 50.0
#define RPM_SNR_MIN_RATIO 3.0f 
bool RPM_IsSignalReliable(double *magnitude, int n, float sampleRate, float *snrOut) {
    float freqResolution = sampleRate / n;
    int binMin = (int)(FR_MIN_HZ / freqResolution);
    int binMax = (int)(FR_MAX_HZ / freqResolution);

    float peakAmp = 0;
    for (int i = binMin; i <= binMax && i < n / 2; i++) {
        if (magnitude[i] > peakAmp) peakAmp = magnitude[i];
    }

    // PERBAIKAN: noise floor sekarang pakai MEDIAN, BUKAN rata-rata.
    //
    // Kenapa rata-rata bermasalah: mesin/kipas nyata yang bergetar KUAT
    // menghasilkan harmonik (2x, 3x RPM, blade-pass frequency, dll) yang
    // jatuh DI LUAR band 5-50Hz tapi ikut dihitung sebagai "noise" di sini.
    // Harmonik-harmonik ini menaikkan rata-rata secara signifikan --
    // akibatnya SNR (peak/noiseFloor) justru JATUH persis ketika sinyal
    // putaran asli sedang KUAT (misal sensor ditempel rigid ke logam
    // kipas). Sebaliknya, getaran lemah/acak (sensor dipegang di udara)
    // hampir tidak punya harmonik, rata-ratanya rendah, SNR gampang lolos
    // walau itu cuma noise tangan, bukan RPM asli. Median jauh lebih tahan
    // terhadap beberapa bin harmonik yang menjulang tinggi seperti ini.
    static double noiseSamples[256]; // cukup untuk n/2 bin (FFT_SAMPLES maks 512)
    int noiseCount = 0;
    for (int i = 1; i < binMin && noiseCount < 256; i++) noiseSamples[noiseCount++] = magnitude[i];
    for (int i = binMax + 1; i < n / 2 && noiseCount < 256; i++) noiseSamples[noiseCount++] = magnitude[i];

    float noiseFloor = 1e-6f;
    if (noiseCount > 0) {
        std::sort(noiseSamples, noiseSamples + noiseCount);
        noiseFloor = (float)noiseSamples[noiseCount / 2]; // median
        if (noiseFloor < 1e-6f) noiseFloor = 1e-6f;
    }

    float snr = peakAmp / noiseFloor;
    if (snrOut != NULL) *snrOut = snr;

    // DEBUG: WAJIB diperhatikan dulu sebelum menyimpulkan sudah fix atau
    // belum. Bandingkan angka peakAmp/noiseFloor/snr ini pas sensor
    // ditempel vs dilepas dari kipas -- kalau snr pas ditempel sekarang
    // JAUH lebih tinggi dari snr pas dilepas, berarti perbaikan ini kerja.
    Serial.printf("[RPM-DEBUG] peakAmp=%.4f noiseFloor(median)=%.4f snr=%.2f (butuh >= %.1f)\n",
                  peakAmp, noiseFloor, snr, RPM_SNR_MIN_RATIO);

    // Threshold empiris: puncak harus minimal 3x lebih tinggi dari noise floor
    // supaya dianggap sinyal putaran nyata, bukan kebetulan noise tertinggi.
    // WAJIB dikalibrasi ulang pakai data motor nyata kalian — angka 3.0 ini
    // starting point, bukan angka final. Uji: motor mati vs motor nyala,
    // print SNR keduanya, tentukan threshold yang memisahkan dua kondisi jelas.
    return snr >= RPM_SNR_MIN_RATIO;
}
float RPM_Estimate(double *magnitude, int n, float sampleRate) {
    // Resolusi frekuensi per bin FFT = sampleRate / jumlah sample.
    // Ini nentuin seberapa presisi kita bisa bedain 1 frekuensi dari frekuensi
    // di sebelahnya. Semakin banyak sample (n), semakin presisi, tapi juga
    // semakin lambat responnya (window waktu lebih panjang).
    float freqResolution = sampleRate / n;

    // Konversi batas Hz ke index bin FFT, karena kita nyari di array bin
    // bukan langsung di domain Hz.
    int binMin = (int)(FR_MIN_HZ / freqResolution);
    int binMax = (int)(FR_MAX_HZ / freqResolution);

    // Cari bin dengan amplitudo tertinggi HANYA di rentang bin yang masuk akal.
    // Ini asumsi fisika: motor manapun, walau idealnya balanced, selalu ada
    // sedikit unbalance alami, sehingga puncak amplitudo dominan biasanya
    // muncul tepat di frekuensi putar motornya sendiri (1x RPM).
    float maxAmplitude = 0;
    int maxBinIndex = binMin;

    for (int i = binMin; i <= binMax && i < n / 2; i++) {
        // n/2 karena spektrum FFT simetris, cuma separuh pertama yang punya
        // informasi frekuensi unik (Nyquist).
        if (magnitude[i] > maxAmplitude) {
            maxAmplitude = magnitude[i];
            maxBinIndex = i;
        }
    }

    // PERBAIKAN PRESISI: tanpa interpolasi, RPM dibulatkan ke kelipatan
    // freqResolution terdekat (misal tiap ~1.95Hz = ~117 RPM sekali "lompat").
    // Interpolasi parabolik pakai 2 bin tetangga (kiri & kanan puncak) buat
    // menaksir posisi puncak SEBENARNYA di antara bin, jadi RPM tidak
    // terpotong-potong ke kelipatan resolusi bin.
    float interpolatedBin = (float)maxBinIndex;
    if (maxBinIndex > 0 && maxBinIndex < (n / 2) - 1) {
        float alpha = (float)magnitude[maxBinIndex - 1];
        float beta  = (float)magnitude[maxBinIndex];
        float gamma = (float)magnitude[maxBinIndex + 1];
        float denom = (alpha - 2.0f * beta + gamma);
        if (fabsf(denom) > 1e-9f) {
            float p = 0.5f * (alpha - gamma) / denom; // pergeseran sub-bin, rentang -0.5..0.5
            interpolatedBin = (float)maxBinIndex + p;
        }
    }

    // Konversi index bin (sudah diinterpolasi) balik ke frekuensi (Hz), lalu ke RPM (x60)
    float fr_hz = interpolatedBin * freqResolution;
    float rpm = fr_hz * 60.0;

    return rpm;
}
float RPM_ComputeBPFO(float fr_hz, int n_balls, float d_ball, float D_pitch, float phi_deg) {
    float phi_rad = phi_deg * (PI / 180.0f);
    return (n_balls / 2.0f) * fr_hz * (1.0f - (d_ball / D_pitch) * cos(phi_rad));
}

float RPM_ComputeBPFI(float fr_hz, int n_balls, float d_ball, float D_pitch, float phi_deg) {
    float phi_rad = phi_deg * (PI / 180.0f);
    return (n_balls / 2.0f) * fr_hz * (1.0f + (d_ball / D_pitch) * cos(phi_rad));
}
