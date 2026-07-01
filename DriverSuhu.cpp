#include "DriverSuhu.h"
#include <OneWire.h>
#include <DallasTemperature.h>

#define DS18B20_PIN 5
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);

struct DataSistem { volatile float suhu; volatile float arusRMS; volatile float getaranRMS; volatile float voldb; };

void TaskDriverSuhu(void *pvParameters) {
    DataSistem *shared = (DataSistem *)pvParameters;
    sensors.begin();
    sensors.setResolution(12);
    sensors.setWaitForConversion(false);
    
    float lastValidSuhu = 27.0;
    const float MAX_DELTA = 1.5000;

    for (;;) {
        sensors.requestTemperatures();
        vTaskDelay(pdMS_TO_TICKS(750)); 
        
        float raw = sensors.getTempCByIndex(0);
        if (raw != -127.00 && raw != 85.00) {
            // Slew Rate Limiter
            if (abs(raw - lastValidSuhu) <= MAX_DELTA) {
                lastValidSuhu = raw;
            } else {
                lastValidSuhu += (raw > lastValidSuhu) ? MAX_DELTA : -MAX_DELTA;
            }
            shared->suhu = lastValidSuhu;
        }
    }
}