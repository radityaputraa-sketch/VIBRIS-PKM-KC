#include "DriverSuhu.h"
#include "DriverArus.h"
#include "DriverGetaran.h"
#include "DriverINM.h"

struct DataSistem {
    volatile float suhu;
    volatile float arusRMS;
    volatile float getaranRMS;
    volatile float voldb; // Output desibel/intensitas dari INMP441
};
DataSistem dataMesin;

void setup() {
    Serial.begin(115200);
    while(!Serial);
    Serial.println("[SYSTEM] Booting Modular Sensor Core (No TFT)...");

    // Alokasi independen tiap driver di Core 0 dan Core 1
    xTaskCreatePinnedToCore(TaskDriverSuhu,    "Task_Suhu",    3072, &dataMesin, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskDriverArus,    "Task_Arus",    3072, &dataMesin, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskDriverGetaran, "Task_Vibrasi", 4096, &dataMesin, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskDriverINM,     "Task_INM",     6144, &dataMesin, 3, NULL, 1); // I2S di Core 1 agar aman dari jitter

    // Task Monitor lokal untuk mencetak data ke Serial setiap 500ms
    xTaskCreatePinnedToCore([](void *pv){
        DataSistem *shared = (DataSistem *)pv;
        for(;;) {
            Serial.printf("SUHU: %.4f | ARUS: %.4f A | VIB_RMS: %.4f | INM_AMP: %.2f\n", 
                          shared->suhu, shared->arusRMS, shared->getaranRMS, shared->voldb);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }, "Task_Monitor", 4096, &dataMesin, 1, NULL, 1);
}

void loop() {
    vTaskDelete(NULL);
}