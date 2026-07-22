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

//Definisi Tungaal current beraingspec
BearingSpec currentBearingSpec = BEARING_TABLE[BEARING_DEFAULT_INDEX];

static bool hasRollingBearing = true;

void setBearingType(bool rollingBearing) {
    hasRollingBearing = rollingBearing;
}
static float ambientRmsEMA = 0.01f;
static const float AMBIENT_EMA_ALPHA = 0.08f;
static const float ABSOLUTE_FLOOR_MULTIPLIER = 3.0f;
static float stableRPM = 0.0f;
static int reliableStreak = 0;
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
                            float *rpm_out, float *bandEnergies_out, float *snr_out) {
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

    float effectiveSampleRate = (input->actual_rate_hz > 1.0f) ? input->actual_rate_hz : SAMPLE_RATE;
    float snr = 0.0f;
    bool snrReliable = RPM_IsSignalReliable(vReal, FFT_SAMPLES, effectiveSampleRate, &snr);
    if (snr_out) *snr_out = snr;

    float absoluteFloor = ambientRmsEMA * ABSOLUTE_FLOOR_MULTIPLIER;
    bool vibrationStrongEnough = (features->rms_getaran >= absoluteFloor);
    bool reliable = snrReliable && vibrationStrongEnough;

    if (!reliable) {
        reliableStreak = 0;
        stableRPM = 0.0f;
        *rpm_out = 0.0f;
        for (int i = 0; i < 4; i++) bandEnergies_out[i] = 0.0f;
        features->valid = false;
        if (!snrReliable) {
            ambientRmsEMA = (1.0f - AMBIENT_EMA_ALPHA) * ambientRmsEMA + AMBIENT_EMA_ALPHA * features->rms_getaran;
        }
        Serial.printf("[FFT] Tidak reliable (snr=%.2f, rms=%.4f, floor=%.4f) -> RPM=0\n", snr, features->rms_getaran, absoluteFloor);
        return;
    }

    float fr_rpm = RPM_Estimate(vReal, FFT_SAMPLES, effectiveSampleRate);
    reliableStreak++;
    if (reliableStreak >= 2) stableRPM = fr_rpm;
    *rpm_out = stableRPM;

    float fr_hz = fr_rpm / 60.0;
    float freqRes = effectiveSampleRate / FFT_SAMPLES;

    bandEnergies_out[0] = bandEnergy(vReal, freqRes, 0.9f * fr_hz, 1.1f * fr_hz, FFT_SAMPLES);
    bandEnergies_out[1] = bandEnergy(vReal, freqRes, 1.9f * fr_hz, 2.1f * fr_hz, FFT_SAMPLES);

    if (hasRollingBearing) {
        float bpfo_hz = RPM_ComputeBPFO(fr_hz, currentBearingSpec.n_balls,
            currentBearingSpec.d_ball_mm, currentBearingSpec.D_pitch_mm, currentBearingSpec.phi_deg);
        float bpfi_hz = RPM_ComputeBPFI(fr_hz, currentBearingSpec.n_balls,
            currentBearingSpec.d_ball_mm, currentBearingSpec.D_pitch_mm, currentBearingSpec.phi_deg);
        bandEnergies_out[2] = bandEnergy(vReal, freqRes, 0.9f * bpfo_hz, 1.1f * bpfo_hz, FFT_SAMPLES);
        bandEnergies_out[3] = bandEnergy(vReal, freqRes, 0.9f * bpfi_hz, 1.1f * bpfi_hz, FFT_SAMPLES);
        void setBearingType(bool rollingBearing);
    } else {
        bandEnergies_out[2] = 0.0f;
        bandEnergies_out[3] = 0.0f;
    }

    features->valid = true;
}