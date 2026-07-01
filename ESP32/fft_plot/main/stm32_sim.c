// SIMULADOR STM32 — REMOVER QUANDO O HARDWARE ESTIVER PRONTO
// Conexão física: GPIO 4 (TX) ──fio──> GPIO 16 (RX do uart_fft)

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_random.h"
#include "fft_frame.h"
#include "stm32_sim.h"

#define SIM_UART    UART_NUM_1
#define SIM_TX_GPIO 4
#define SIM_BAUD    115200
#define SAMPLE_RATE 44100

static void stm32_sim_task(void *arg)
{
    static fft_frame_t frame;
    frame.sync[0]     = FRAME_SYNC_0;
    frame.sync[1]     = FRAME_SYNC_1;
    frame.sample_rate = SAMPLE_RATE;

    uint32_t tick = 0;
    while (1) {
        int peak_pos = 20 + (tick % (FFT_BINS - 40));
        for (int i = 0; i < FFT_BINS; i++) {
            float d1 = (i - peak_pos) / 6.0f;
            float d2 = (i - 120)      / 12.0f;
            float p1    = expf(-0.5f * d1 * d1);
            float p2    = expf(-0.5f * d2 * d2);
            float noise = (float)(esp_random() % 300) / 10000.0f;
            float v     = (p1 * 0.9f + p2 * 0.5f + noise) * 60000.0f;
            frame.data[i] = (uint16_t)fminf(v, 65535.0f);
        }
        uart_write_bytes(SIM_UART, (const char *)&frame, FRAME_TOTAL);
        tick++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void stm32_sim_start(void)
{
    uart_config_t cfg = {
        .baud_rate  = SIM_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(SIM_UART, 256, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(SIM_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(SIM_UART, SIM_TX_GPIO, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreate(stm32_sim_task, "stm32_sim", 4096, NULL, 5, NULL);
    ESP_LOGI("stm32_sim", "UART%d TX=GPIO%d @ %d baud", SIM_UART, SIM_TX_GPIO, SIM_BAUD);
}
