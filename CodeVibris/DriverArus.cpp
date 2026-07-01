#include "DriverArus.h"

#define SCT_PIN 4
struct DataSistem { volatile float suhu; volatile float arusRMS; volatile float getaranRMS; volatile float voldb; };

void TaskDriverArus(void *pvParameters) {
    DataSistem *shared = (DataSistem *)pvParameters;
    pinMode(SCT_PIN, INPUT);

    for (;;) {
        long sum = 0;
        uint32_t samples = 500;
        
        // Root Mean Square (RMS) Calculation loop
        for (uint32_t i = 0; i < samples; i++) {
            long val = analogRead(SCT_PIN) - 2048; // Offset VCC/2 untuk ESP32 (12-bit ADC)
            sum += (val * val);
            esp_rom_delay_us(100); 
        }
        
        float rmsOffset = sqrt(sum / samples);
        float kalkulasiArus = (rmsOffset * 0.025); // Kalibrasi faktor sesuai beban pasar fisik Anda
        shared->arusRMS = kalkulasiArus;
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
