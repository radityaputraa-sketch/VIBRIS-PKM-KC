#include "DriverINM.h"
#include <driver/i2s.h>

#define I2S_SD 16
#define I2S_WS 17
#define I2S_SCK 18

struct DataSistem { volatile float suhu; volatile float arusRMS; volatile float getaranRMS; volatile float voldb; };

void TaskDriverINM(void *pvParameters) {
    DataSistem *shared = (DataSistem *)pvParameters;
    
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
        .use_apll = false
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);

    // FIX: Menggunakan static agar dialokasikan di luar stack task
    static int32_t i2s_buffer[1024]; 
    size_t bytes_read;

    for (;;) {
        i2s_read(I2S_NUM_0, &i2s_buffer, sizeof(i2s_buffer), &bytes_read, portMAX_DELAY);
        int samples_read = bytes_read / 4;
        
        int32_t maxVal = -2147483648;
        int32_t minVal = 2147483647;

        for (int i = 0; i < samples_read; i++) {
            int32_t sample = i2s_buffer[i] >> 14; 
            if(sample > maxVal) maxVal = sample;
            if(sample < minVal) minVal = sample;
        }
        
        if(samples_read > 0) {
            long long peakToPeak = (long long)maxVal - minVal;
            shared->voldb = (float)peakToPeak; 
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}