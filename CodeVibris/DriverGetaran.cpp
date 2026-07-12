// DriverGetaran.cpp
#include "DriverGetaran.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <math.h>
#include "DualCoreTaskScheduler.h"

portMUX_TYPE getaranMux = portMUX_INITIALIZER_UNLOCKED;
static TwoWire I2CLis3dh = TwoWire(0);
static Adafruit_LIS3DH lis3dhInstance = Adafruit_LIS3DH(&I2CLis3dh);

void TaskDriverGetaran(void *pvParameters) {
    (void)pvParameters;

    I2CLis3dh.begin(PIN_LIS3DH_SDA, PIN_LIS3DH_SCL, 400000);

    if (!lis3dhInstance.begin(0x18)) {
        for (;;) {
            Serial.println(F("[ERROR] LIS3DH Tidak Terdeteksi!"));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    lis3dhInstance.setRange(LIS3DH_RANGE_4_G);
    lis3dhInstance.setDataRate(LIS3DH_DATARATE_LOWPOWER_5KHZ);  // aktual ~1.25kHz Normal HR mode (bug library #14), tetap 12-bit

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xSamplingPeriod = pdMS_TO_TICKS((uint32_t)VIBRATION_SAMPLE_PERIOD_MS);

    const float alpha = 0.98f;
    float filteredMagnitudeOld = 0.0f;
    float rawMagnitudeOld = 0.0f;
    static VibrationBuffer localVibBuffer;

    // Deteksi "sensor macet": kalau I2C gagal baca atau bacaan mentahnya
    // persis sama berkali-kali berturut-turut, rumus selisih di bawah akan
    // menghasilkan 0.00 terus meski mesin sebenarnya bergetar. Ini BUKAN
    // kondisi normal (noise sensor asli tidak pernah benar-benar 0.000000
    // persis), jadi kita hitung dan laporkan lewat Serial supaya kelihatan
    // jelas ini masalah sambungan sensor, bukan cuma "mesin diam".
    int stuckReadingStreak = 0;
    const int STUCK_WARNING_THRESHOLD = 50; // ~50 sample berturut-turut identik

    for (;;) {
        sensors_event_t event;

        for (int i = 0; i < FFT_SAMPLES; i++) {
            bool readOk = lis3dhInstance.getEvent(&event);

            float ax = event.acceleration.x;
            float ay = event.acceleration.y;
            float az = event.acceleration.z;

            float rawMagnitude = sqrt((ax * ax) + (ay * ay) + (az * az));

            if (!readOk || rawMagnitude == rawMagnitudeOld) {
                stuckReadingStreak++;
            } else {
                stuckReadingStreak = 0;
            }

            if (stuckReadingStreak == STUCK_WARNING_THRESHOLD) {
                Serial.println(F("[WARNING] Sensor getaran (LIS3DH) kemungkinan MACET: "
                                  "bacaan I2C sama persis berkali-kali. Cek sambungan "
                                  "SDA/SCL & solderan modul sensor — ini penyebab paling "
                                  "umum grafik Vibration terlihat datar/flat."));
            }

            float dynamicVibration = alpha * (filteredMagnitudeOld + rawMagnitude - rawMagnitudeOld);

            filteredMagnitudeOld = dynamicVibration;
            rawMagnitudeOld = rawMagnitude;

            localVibBuffer.samples[i] = dynamicVibration;
            vTaskDelayUntil(&xLastWakeTime, xSamplingPeriod);
        }

        QueueHandle_t q = Scheduler_GetVibrationQueue();
        if (q != NULL) {
            xQueueSend(q, &localVibBuffer, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
