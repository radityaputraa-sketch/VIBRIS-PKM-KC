// AdaptiveBaselineLearner.h
#pragma once
#include "SharedTypes.h"

/*
MODUL: Adaptive Baseline Learner

Ini modul yang membuat klaim Self-Baseline Learning di judul
proposal benar-benar valid secara teknis, bukan sekadar istilah
marketing.

MASALAH YANG DISELESAIKAN:
Kalau baseline, yaitu mu dan Sigma, hanya dihitung sekali di awal
lalu dipakai selamanya tanpa berubah, sistem akan makin sering
salah deteksi, false alarm, seiring waktu. Alasannya, mesin rotasi
punya perubahan kondisi wajar yang bertahap dan bukan tanda
kerusakan, misalnya karakteristik bearing baru yang sedikit
berbeda dari bearing yang sudah dipakai dua bulan meski keduanya
masih sehat. Sistem dengan baseline statis akan menganggap
perubahan wajar semacam ini sebagai anomali padahal bukan.

METODE YANG DIPAKAI: Exponential Moving Average (EMA)
Setiap siklus deteksi yang hasilnya Normal, nilai mu dan Sigma
diperbarui sedikit demi sedikit mengikuti data terbaru, dengan
bobot kecil (learning rate) supaya perubahan berjalan pelan dan
tidak reaktif terhadap noise sesaat.

ATURAN PALING PENTING DI MODUL INI:
Baseline hanya diperbarui kalau status deteksi saat itu adalah
Normal. Kalau sistem sedang mendeteksi kondisi Waspada atau
Bahaya, update baseline tidak dilakukan. Ini untuk mencegah
skenario di mana kerusakan yang berkembang bertahap justru
dipelajari sistem sebagai kondisi normal baru, yang akan membuat
sistem makin tidak sensitif terhadap masalah yang sebenarnya
sedang terjadi. Guard ini yang membuat mekanisme self-baseline
aman dipakai, bukan cuma mengikuti data secara membabi buta.

CATATAN PERFORMA:
Invers matriks Sigma tidak dihitung ulang di setiap siklus update
karena operasi ini tergolong berat secara komputasi. Invers hanya
dihitung ulang secara berkala, misalnya tiap beberapa puluh
sample, sementara mu dan Sigma mentah tetap diperbarui setiap
siklus.
*/
bool isBaselineLearnerReady();
void initializeBaselineLearner(float initialMean[4], float initialSigma[4][4]);
void updateBaselineIfNormal(float currentFeatures[4], bool isCurrentStatusNormal);
void getCurrentBaseline(float meanOutput[4], float sigmaInverseOutput[4][4]);