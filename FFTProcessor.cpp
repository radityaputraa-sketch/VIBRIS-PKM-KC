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

    // float sumSquare = 0;
    // for (int i = 0; i < FFT_SAMPLES; i++) sumSquare += input->samples[i] * input->samples[i];
    // features->rms_getaran = sqrt(sumSquare / FFT_SAMPLES);

    // float fr_rpm = RPM_Estimate(vReal, FFT_SAMPLES, SAMPLE_RATE);
    // *rpm_out = fr_rpm;
    // float fr_hz = fr_rpm / 60.0;

    // float freqRes = SAMPLE_RATE / FFT_SAMPLES;
    // bandEnergies_out[0] = bandEnergy(vReal, freqRes, 0.9f * fr_hz, 1.1f * fr_hz, FFT_SAMPLES);
    // bandEnergies_out[1] = bandEnergy(vReal, freqRes, 1.9f * fr_hz, 2.1f * fr_hz, FFT_SAMPLES);

    // float bpfo_hz = RPM_ComputeBPFO(fr_hz, 8, 3.5f, 22.0f, 0.0f);
    // float bpfi_hz = RPM_ComputeBPFI(fr_hz, 8, 3.5f, 22.0f, 0.0f);

    // bandEnergies_out[2] = bandEnergy(vReal, freqRes, 0.9f * bpfo_hz, 1.1f * bpfo_hz, FFT_SAMPLES);
    // bandEnergies_out[3] = bandEnergy(vReal, freqRes, 0.9f * bpfi_hz, 1.1f * bpfi_hz, FFT_SAMPLES);

    // features->valid = true;

    
    float sumSquare = 0;
    for (int i = 0; i < FFT_SAMPLES; i++) sumSquare += input->samples[i] * input->samples[i];
    features->rms_getaran = sqrt(sumSquare / FFT_SAMPLES);

    float snr = 0.0f;
    bool reliable = RPM_IsSignalReliable(vReal, FFT_SAMPLES, SAMPLE_RATE, &snr);

    if (!reliable) {
        // Tidak ada mesin berputar terdeteksi — jangan hasilkan RPM/band energy palsu.
        *rpm_out = 0.0f;
        for (int i = 0; i < 4; i++) bandEnergies_out[i] = 0.0f;
        features->valid = false;   // penting: ini yang mencegah sample masuk kalibrasi
        Serial.printf("[FFT] SNR=%.2f terlalu rendah, sinyal putaran tidak terdeteksi.\n", snr);
        return;
    }

    float fr_rpm = RPM_Estimate(vReal, FFT_SAMPLES, SAMPLE_RATE);
    *rpm_out = fr_rpm;
    float fr_hz = fr_rpm / 60.0;

    float freqRes = SAMPLE_RATE / FFT_SAMPLES;
    bandEnergies_out[0] = bandEnergy(vReal, freqRes, 0.9f * fr_hz, 1.1f * fr_hz, FFT_SAMPLES);
    bandEnergies_out[1] = bandEnergy(vReal, freqRes, 1.9f * fr_hz, 2.1f * fr_hz, FFT_SAMPLES);

    float bpfo_hz = RPM_ComputeBPFO(fr_hz, 8, 3.5f, 22.0f, 0.0f);
    float bpfi_hz = RPM_ComputeBPFI(fr_hz, 8, 3.5f, 22.0f, 0.0f);
    bandEnergies_out[2] = bandEnergy(vReal, freqRes, 0.9f * bpfo_hz, 1.1f * bpfo_hz, FFT_SAMPLES);
    bandEnergies_out[3] = bandEnergy(vReal, freqRes, 0.9f * bpfi_hz, 1.1f * bpfi_hz, FFT_SAMPLES);

    features->valid = true;

}