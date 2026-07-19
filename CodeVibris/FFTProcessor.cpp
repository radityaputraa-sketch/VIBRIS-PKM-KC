// FFTProcessor.cpp
#include "FFTProcessor.h"
#include <arduinoFFT.h>
#include <math.h>
#include "RPMEstimator.h"
#include "config.h"

#define SAMPLE_RATE VIBRATION_SAMPLE_RATE_HZ
#define FR_MIN_HZ 5.0
#define FR_MAX_HZ 50.0

double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SAMPLES, SAMPLE_RATE);

void FFTProcessor_Init() {}

float bandEnergy(double *magnitude, float freqResolution, float f_low, float f_high, int n) {
    int binLow = (int)(f_low / freqResolution);
    int binHigh = (int)(f_high / freqResolution);
    float energy = 0;
    for (int i = binLow; i <= binHigh && i < n/2; i++) {
        energy += magnitude[i] * magnitude[i];
    }
    return energy;
}

// ADAPTIF, BUKAN ANGKA TETAP: daripada nebak satu angka RMS "diam" yang harus
// pas buat SEMUA mesin (kipas 1500rpm, pompa 2800rpm, dst -- getarannya beda
// jauh), sistem ini BELAJAR SENDIRI level "diam/noise" dari sensor kalian
// tiap kali dipakai. Caranya: tiap batch yang SNR-nya sendiri sudah bilang
// "tidak ada puncak jelas" (snrReliable == false), rms_getaran batch itu
// dianggap sampel noise ambient, dan baseline-nya di-update pelan-pelan
// (exponential moving average). Ambang absolut = baseline ambient x kelipatan
// aman. Ini otomatis menyesuaikan diri ke mesin/mounting APA PUN tanpa perlu
// dikalibrasi manual per alat.
static float ambientRmsEMA = 0.01f;                 // baseline awal (tebakan kasar), langsung ter-update begitu dipakai
static const float AMBIENT_EMA_ALPHA = 0.08f;        // kecepatan adaptasi baseline (0..1, makin besar makin cepat ikut berubah)
static const float ABSOLUTE_FLOOR_MULTIPLIER = 3.0f; // getaran nyata harus >= 3x baseline diam buat dianggap rotasi asli

// Debounce/hysteresis: RPM baru HANYA diterima kalau reliable (SNR + RMS lolos)
// SELAMA 2 batch berturut-turut. Ini mencegah satu batch noise yang kebetulan
// lolos SNR nyeplos jadi angka RPM aneh sesaat (misal 0 -> 2000 -> 0 dalam
// hitungan detik). Sebaliknya, begitu 2 batch berturut-turut TIDAK reliable,
// RPM langsung di-drop ke 0 (tidak perlu debounce buat "mati", karena "mati"
// itu keadaan aman -- lebih baik cepat bilang 0 daripada lambat).
static float stableRPM = 0.0f;
static int reliableStreak = 0;

void FFTProcessor_Process(VibrationBuffer *input, SensorFeatures *features,
                            float *rpm_out, float *bandEnergies_out) {
    double mean = 0;
    for (int i = 0; i < FFT_SAMPLES; i++) mean += input->samples[i];
    mean /= FFT_SAMPLES;

    for (int i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] = input->samples[i] - mean;
        vImag[i] = 0;
    }

    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    float sumSquare = 0;
    for (int i = 0; i < FFT_SAMPLES; i++) sumSquare += input->samples[i] * input->samples[i];
    features->rms_getaran = sqrt(sumSquare / FFT_SAMPLES);

    float snr = 0.0f;
    // PENTING: pakai rate yang BENAR-BENAR tercapai (diukur live di
    // DriverGetaran.cpp), bukan angka asumsi VIBRATION_SAMPLE_RATE_HZ di
    // config.h. Kalau sensor cuma sanggup mencapai rate berbeda dari target
    // (umum terjadi di LIS3DH karena keterbatasan I2C/library), memakai
    // angka asumsi di sini bikin freqResolution salah -> RPM meleset
    // proporsional dengan besarnya selisih rate itu.
    float effectiveSampleRate = (input->actual_rate_hz > 1.0f) ? input->actual_rate_hz : SAMPLE_RATE;
    bool snrReliable = RPM_IsSignalReliable(vReal, FFT_SAMPLES, effectiveSampleRate, &snr);

    // Gerbang KEDUA: getaran absolut harus di atas ambang "diam" yang
    // dipelajari otomatis, bukan cuma SNR relatif yang lolos.
    float absoluteFloor = ambientRmsEMA * ABSOLUTE_FLOOR_MULTIPLIER;
    bool vibrationStrongEnough = (features->rms_getaran >= absoluteFloor);
    bool reliable = snrReliable && vibrationStrongEnough;

    if (!reliable) {
        reliableStreak = 0;
        stableRPM = 0.0f; // langsung drop ke 0, tidak perlu tunggu debounce buat kondisi "diam"
        *rpm_out = 0.0f;
        for (int i = 0; i < 4; i++) bandEnergies_out[i] = 0.0f;
        features->valid = false;   // penting: ini yang mencegah sample masuk kalibrasi

        // Hanya update baseline ambient kalau SNR SENDIRI juga bilang "tidak
        // ada puncak" -- jadi baseline tidak ikut tercemar oleh batch yang
        // sebenarnya rotasi asli tapi kebetulan gagal di gerbang RMS.
        if (!snrReliable) {
            ambientRmsEMA = (1.0f - AMBIENT_EMA_ALPHA) * ambientRmsEMA
                           + AMBIENT_EMA_ALPHA * features->rms_getaran;
        }

        Serial.printf("[FFT] Tidak reliable (snr=%.2f, snrOK=%d, rms=%.4f, floor=%.4f, ambientEMA=%.4f) -> RPM=0\n",
                      snr, snrReliable, features->rms_getaran, absoluteFloor, ambientRmsEMA);
        return;
    }

    float fr_rpm = RPM_Estimate(vReal, FFT_SAMPLES, effectiveSampleRate);

    reliableStreak++;
    if (reliableStreak >= 2) {
        // Sudah 2x berturut-turut reliable -> baru dipercaya sebagai RPM baru.
        stableRPM = fr_rpm;
    }
    // Batch reliable PERTAMA (streak==1) sengaja BELUM mengubah stableRPM,
    // masih menahan nilai lama (atau 0 kalau sebelumnya diam) -- ini yang
    // mencegah satu batch "beruntung" langsung nyeplos jadi angka RPM baru.

    *rpm_out = stableRPM;
    float fr_hz = fr_rpm / 60.0; // band energy tetap pakai estimasi batch ini (bukan yang di-debounce)

    Serial.printf("[FFT] snr=%.2f rms=%.4f floor=%.4f -> fr_rpm(batch)=%.1f | stableRPM(output)=%.1f (streak=%d)\n",
                  snr, features->rms_getaran, absoluteFloor, fr_rpm, stableRPM, reliableStreak);

    float freqRes = effectiveSampleRate / FFT_SAMPLES;
    bandEnergies_out[0] = bandEnergy(vReal, freqRes, 0.9f * fr_hz, 1.1f * fr_hz, FFT_SAMPLES);
    bandEnergies_out[1] = bandEnergy(vReal, freqRes, 1.9f * fr_hz, 2.1f * fr_hz, FFT_SAMPLES);

    float bpfo_hz = RPM_ComputeBPFO(fr_hz, 8, 3.5f, 22.0f, 0.0f);
    float bpfi_hz = RPM_ComputeBPFI(fr_hz, 8, 3.5f, 22.0f, 0.0f);
    bandEnergies_out[2] = bandEnergy(vReal, freqRes, 0.9f * bpfo_hz, 1.1f * bpfo_hz, FFT_SAMPLES);
    bandEnergies_out[3] = bandEnergy(vReal, freqRes, 0.9f * bpfi_hz, 1.1f * bpfi_hz, FFT_SAMPLES);

    features->valid = true;

}
