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

    bool sensorOK = lis3dhInstance.begin(0x18);

    if (!sensorOK) {
        sensorOK = lis3dhInstance.begin(0x19);
    }

    if (!sensorOK) {
        Serial.println(F("[ERROR] LIS3DH tidak ditemukan pada alamat 0x18 maupun 0x19"));

        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    Serial.println(F("[SYSTEM] LIS3DH berhasil terdeteksi."));
    lis3dhInstance.setRange(LIS3DH_RANGE_4_G);
    lis3dhInstance.setDataRate(LIS3DH_DATARATE_LOWPOWER_5KHZ);  // aktual ~1.25kHz Normal HR mode (bug library #14), tetap 12-bit

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xSamplingPeriod = pdMS_TO_TICKS((uint32_t)VIBRATION_SAMPLE_PERIOD_MS);

    const float alpha = 0.98f;
    float filteredMagnitudeOld = 0.0f;
    float rawMagnitudeOld = 0.0f;
    static VibrationBuffer localVibBuffer;

    for (;;) {
        sensors_event_t event;

        for (int i = 0; i < FFT_SAMPLES; i++) {
            lis3dhInstance.getEvent(&event);

            float ax = event.acceleration.x;
            float ay = event.acceleration.y;
            float az = event.acceleration.z;

            float rawMagnitude = sqrt((ax * ax) + (ay * ay) + (az * az));
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
