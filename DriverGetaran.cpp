#include "DriverGetaran.h"
#include <Wire.h>
#include <Adafruit_LIS3DH.h>

#define SDA_PIN 6
#define SCL_PIN 7
Adafruit_LIS3DH lis = Adafruit_LIS3DH();

struct DataSistem { volatile float suhu; volatile float arusRMS; volatile float getaranRMS; volatile float voldb; };

void TaskDriverGetaran(void *pvParameters) {
    DataSistem *shared = (DataSistem *)pvParameters;
    Wire.begin(SDA_PIN, SCL_PIN);
    
    if (!lis.begin(0x18)) { // Cek alamat I2C sensor (0x18 atau 0x19)
        while (1) vTaskDelay(pdMS_TO_TICKS(100)); 
    }
    lis.setRange(LIS3DH_RANGE_4_G);

    for (;;) {
        float sumSquare = 0;
        const int samples = 100;
        
        for(int i=0; i<samples; i++) {
            lis.read();
            float z = lis.z_g; // Fokus pada aksis vertikal beban getaran mesin
            sumSquare += (z * z);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        
        shared->getaranRMS = sqrt(sumSquare / samples);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}