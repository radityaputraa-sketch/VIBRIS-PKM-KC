// InitialBaselineCalibrator.h
#pragma once
#include "SharedTypes.h"

/*
MODUL: Initial Baseline Calibrator

Menangani proses kalibrasi awal sistem, yaitu fase pengambilan
data kondisi normal mesin selama kurang lebih 60 detik pertama
setelah device dinyalakan, sesuai prosedur pengujian yang tertulis
di proposal BAB 3.3.

TUJUAN FASE INI:
Membentuk profil normal awal mesin, yaitu mean dan matriks
kovarians dari empat fitur sensor (RMS getaran, RMS suara, arus,
suhu), tanpa memerlukan dataset berlabel dari luar. Profil inilah
yang jadi acuan pertama sebelum sistem masuk mode deteksi
real-time.

BEDA DENGAN AdaptiveBaselineLearner:
Modul ini hanya berjalan sekali di awal, one-time calibration,
menghasilkan nilai mu dan Sigma pertama. Setelah itu, tugas
memperbarui baseline secara berkelanjutan diambil alih oleh
AdaptiveBaselineLearner. Pemisahan ini disengaja supaya proses
kalibrasi awal, yang harus dilakukan dengan hati-hati memastikan
mesin benar-benar dalam kondisi normal saat direkam, tidak
tercampur dengan logic pembaruan berkelanjutan yang berjalan
otomatis di background selama device beroperasi.

PENYIMPANAN HASIL KALIBRASI:
Hasil kalibrasi, yaitu mu dan Sigma^-1, disimpan ke flash memory
(Preferences/NVS ESP32) supaya tidak hilang kalau device restart
atau baterai diganti. Tanpa penyimpanan ini, device harus
mengulang kalibrasi 60 detik setiap kali dinyalakan ulang, yang
tidak praktis untuk penggunaan sehari-hari oleh UMKM.
*/

void startCalibrationPhase();
bool addCalibrationSample(SensorFeatures sample);
void computeInitialBaseline(float meanOutput[4], float sigmaInverseOutput[4][4]);
void saveBaselineToFlash(float mean[4], float sigmaInverse[4][4]);
bool loadBaselineFromFlash(float meanOutput[4], float sigmaInverseOutput[4][4]);
bool isLastCalibrationValid();
bool addBandEnergyCalibrationSample(float bandEnergies[4]);
void computeBandEnergyBaseline(float meanOutput[4], float stdOutput[4]);
void saveBandBaselineToFlash(float mean[4], float std[4]);
bool loadBandBaselineFromFlash(float meanOutput[4], float stdOutput[4]);