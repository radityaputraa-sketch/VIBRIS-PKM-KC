#include "RPMEstimator.h"
#include <math.h>
#include <Arduino.h>

// Batas rentang RPM motor kecil sesuai proposal (300-3000 RPM = 5-50 Hz)
// Di luar rentang ini diabaikan supaya nggak salah tangkap noise frekuensi
// rendah (getaran lingkungan, goyangan meja) atau harmonik tinggi yang
// bukan representasi RPM asli.
#define FR_MIN_HZ 5.0
#define FR_MAX_HZ 50.0
bool RPM_IsSignalReliable(double *magnitude, int n, float sampleRate, float *snrOut) {
    float freqResolution = sampleRate / n;
    int binMin = (int)(FR_MIN_HZ / freqResolution);
    int binMax = (int)(FR_MAX_HZ / freqResolution);

    float peakAmp = 0;
    for (int i = binMin; i <= binMax && i < n / 2; i++) {
        if (magnitude[i] > peakAmp) peakAmp = magnitude[i];
    }

    // Noise floor: median-ish estimate pakai rata-rata bin DI LUAR rentang RPM plausible
    // (bin sangat rendah + sangat tinggi), representasi noise dasar sensor.
    double noiseSum = 0;
    int noiseCount = 0;
    for (int i = 1; i < binMin; i++) { noiseSum += magnitude[i]; noiseCount++; }
    for (int i = binMax + 1; i < n / 2; i++) { noiseSum += magnitude[i]; noiseCount++; }
    float noiseFloor = (noiseCount > 0) ? (float)(noiseSum / noiseCount) : 1.0f;
    if (noiseFloor < 1e-6f) noiseFloor = 1e-6f;

    float snr = peakAmp / noiseFloor;
    if (snrOut != NULL) *snrOut = snr;

    // Threshold empiris: puncak harus minimal 3x lebih tinggi dari noise floor
    // supaya dianggap sinyal putaran nyata, bukan kebetulan noise tertinggi.
    // WAJIB dikalibrasi ulang pakai data motor nyata kalian — angka 3.0 ini
    // starting point, bukan angka final. Uji: motor mati vs motor nyala,
    // print SNR keduanya, tentukan threshold yang memisahkan dua kondisi jelas.
    return snr >= 3.0f;
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

    // Konversi index bin balik ke frekuensi (Hz), lalu ke RPM (x60)
    float fr_hz = maxBinIndex * freqResolution;
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