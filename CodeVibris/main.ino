#include "DriverSuhu.h"
#include "DriverArus.h"
#include "DriverGetaran.h"
#include "DriverINM.h"

struct DataSistem {
    volatile float suhu;
    volatile float arusRMS;
    volatile float getaranRMS;
    volatile float voldb;
};
DataSistem dataMesin;

void setup() {
    Serial.begin(115200);
    while(!Serial);
    Serial.println("[SYSTEM] Booting Modular Sensor Core (JSON Mode)...");

    xTaskCreatePinnedToCore(TaskDriverSuhu,    "Task_Suhu",    3072, &dataMesin, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskDriverArus,    "Task_Arus",    3072, &dataMesin, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskDriverGetaran, "Task_Vibrasi", 4096, &dataMesin, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskDriverINM,     "Task_INM",     6144, &dataMesin, 3, NULL, 1);

    xTaskCreatePinnedToCore([](void *pv){
        DataSistem *shared = (DataSistem *)pv;

        for(;;) {
            String status = "NORMAL";
            if (shared->getaranRMS > 50 || shared->suhu > 80) {
                status = "WARNING";
            }
            if (shared->getaranRMS > 80 || shared->suhu > 100) {
                status = "DANGER";
            }

            Serial.print("{");
            Serial.print("\"suhu\":"); Serial.print(shared->suhu, 2); Serial.print(",");
            Serial.print("\"arus\":"); Serial.print(shared->arusRMS, 2); Serial.print(",");
            Serial.print("\"getaran\":"); Serial.print(shared->getaranRMS, 2); Serial.print(",");
            Serial.print("\"suara\":"); Serial.print(shared->voldb, 2); Serial.print(",");
            Serial.print("\"status\":\""); Serial.print(status); Serial.print("\"");
            Serial.println("}");

            vTaskDelay(pdMS_TO_TICKS(500));
        }

    }, "Task_Monitor_JSON", 4096, &dataMesin, 1, NULL, 1);
}

void loop() {
    vTaskDelete(NULL);
}
