// AudioFFTProcessor.cpp
#include "DriverAudioFFTProcessor.h"
#include <arduinoFFT.h>
#include <math.h>
#include "config.h"

#define AUDIO_SAMPLE_RATE AUDIO_SAMPLE_RATE_HZ

static double aReal[AUDIO_FFT_SAMPLES];
static double aImag[AUDIO_FFT_SAMPLES];
static ArduinoFFT<double> audioFFT = ArduinoFFT<double>(aReal, aImag, AUDIO_FFT_SAMPLES, AUDIO_SAMPLE_RATE);

// ===================================================================
// BATAS BAND -- HEURISTIK AWAL, BELUM DIVALIDASI EMPIRIS.
// Lihat catatan keterbatasan di AudioFFTProcessor.h. Ganti angka ini
// setelah kalian bandingkan spektrum rekaman motor NORMAL vs motor
// dengan kerusakan diketahui (bearing kering, dsb).
// ===================================================================
#define AUDIO_BAND_LOW_MIN_HZ    100.0f   // "rumble" mekanis frekuensi rendah
#define AUDIO_BAND_LOW_MAX_HZ    500.0f
#define AUDIO_BAND_MID_MIN_HZ    500.0f   // dengungan motor normal (baseline)
#define AUDIO_BAND_MID_MAX_HZ   2000.0f
#define AUDIO_BAND_HIGH_MIN_HZ  2000.0f   // gesekan/decitan frekuensi tinggi
#define AUDIO_BAND_HIGH_MAX_HZ  6000.0f

void DriverAudioFFTProcessor_Init() {}

static float audioBandEnergy(double *magnitude, float freqResolution,
                              float f_low, float f_high, int n) {
    int binLow  = (int)(f_low / freqResolution);
    int binHigh = (int)(f_high / freqResolution);
    float energy = 0;
    for (int i = binLow; i <= binHigh && i < n / 2; i++) {
        energy += magnitude[i] * magnitude[i];
    }
    return energy;
}

void DriverAudioFFTProcessor_Process(AudioBuffer *input, float *bandEnergies_out) {
    double mean = 0;
    for (int i = 0; i < AUDIO_FFT_SAMPLES; i++) mean += input->samples[i];
    mean /= AUDIO_FFT_SAMPLES;

    for (int i = 0; i < AUDIO_FFT_SAMPLES; i++) {
        aReal[i] = (double)input->samples[i] - mean;
        aImag[i] = 0;
    }

    audioFFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    audioFFT.compute(FFTDirection::Forward);
    audioFFT.complexToMagnitude();

    float freqRes = (float)AUDIO_SAMPLE_RATE / AUDIO_FFT_SAMPLES;

    bandEnergies_out[0] = audioBandEnergy(aReal, freqRes, AUDIO_BAND_LOW_MIN_HZ,  AUDIO_BAND_LOW_MAX_HZ,  AUDIO_FFT_SAMPLES);
    bandEnergies_out[1] = audioBandEnergy(aReal, freqRes, AUDIO_BAND_MID_MIN_HZ,  AUDIO_BAND_MID_MAX_HZ,  AUDIO_FFT_SAMPLES);
    bandEnergies_out[2] = audioBandEnergy(aReal, freqRes, AUDIO_BAND_HIGH_MIN_HZ, AUDIO_BAND_HIGH_MAX_HZ, AUDIO_FFT_SAMPLES);
}