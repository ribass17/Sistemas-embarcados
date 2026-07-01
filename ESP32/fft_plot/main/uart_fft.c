#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "fft_frame.h"
#include "web_server.h"

static const char *TAG = "uart_fft";

#define UART_NUM    UART_NUM_2
#define UART_BAUD   115200
#define UART_RX_BUF 1024

static QueueHandle_t s_uart_queue;

typedef enum { WAIT_SYNC0, WAIT_SYNC1 } sync_state_t;

static void process_byte(uint8_t byte, sync_state_t *state, fft_frame_t *frame)
{
    switch (*state) {
        case WAIT_SYNC0:
            if (byte == FRAME_SYNC_0) *state = WAIT_SYNC1;
            break;
        case WAIT_SYNC1:
            if (byte == FRAME_SYNC_1) {
                const size_t rest = FRAME_TOTAL - 2;
                int got = uart_read_bytes(UART_NUM, (uint8_t *)frame + 2,
                                          rest, pdMS_TO_TICKS(500));
                *state = WAIT_SYNC0;
                if (got == (int)rest)
                    ws_broadcast_fft((const uint8_t *)frame + 2, rest);
                else
                    ESP_LOGW(TAG, "leitura curta: %d/%d", got, (int)rest);
            } else if (byte == FRAME_SYNC_0) {
                // 0xAA 0xAA: mantém WAIT_SYNC1
            } else {
                *state = WAIT_SYNC0;
            }
            break;
    }
}

static void uart_fft_task(void *arg)
{
    uart_event_t event;
    static fft_frame_t frame;
    sync_state_t state = WAIT_SYNC0;
    uint8_t byte;

    ESP_LOGI(TAG, "Task iniciada (UART%d RX=GPIO%d)", UART_NUM, CONFIG_UART_RX_PIN);

    while (1) {
        if (!xQueueReceive(s_uart_queue, &event, portMAX_DELAY)) continue;
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            uart_flush_input(UART_NUM);
            xQueueReset(s_uart_queue);
            state = WAIT_SYNC0;
            continue;
        }
        if (event.type != UART_DATA) continue;
        while (uart_read_bytes(UART_NUM, &byte, 1, pdMS_TO_TICKS(50)) == 1)
            process_byte(byte, &state, &frame);
    }
}

void uart_fft_start(void)
{
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_RX_BUF, 0, 20, &s_uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, CONFIG_UART_TX_PIN, CONFIG_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreate(uart_fft_task, "uart_fft", 4096, NULL, 12, NULL);
    ESP_LOGI(TAG, "UART%d RX=GPIO%d TX=GPIO%d @ %d baud",
             UART_NUM, CONFIG_UART_RX_PIN, CONFIG_UART_TX_PIN, UART_BAUD);
}
