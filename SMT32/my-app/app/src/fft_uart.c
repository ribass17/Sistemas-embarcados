/*
 * fft_uart_task: FFT 512 pts sobre ADC, envia frame 518 bytes via USART3
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

#include <arm_math.h>

#include "dac_sen.h"

extern struct k_sem adc_ready_sem;
#define ADC_BUF_LEN 512U
extern uint16_t adc_buf[ADC_BUF_LEN];

#define FFT_BINS    256
#define FRAME_TOTAL (2 + 4 + FFT_BINS * 2)   /* 518 bytes */

typedef struct __attribute__((packed)) {
	uint8_t  sync[2];
	uint32_t sample_rate;
	uint16_t data[FFT_BINS];
} fft_frame_t;

K_SEM_DEFINE(uart_tx_sem, 0, 1);

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3));

static float32_t fft_in[ADC_BUF_LEN];
static float32_t fft_out[ADC_BUF_LEN];
static float32_t mag[FFT_BINS];
static fft_frame_t tx_frame;

/* ---------- Callback UART ---------- */
static void uart_cb(const struct device *dev, struct uart_event *evt, void *ud)
{
	ARG_UNUSED(ud);
	ARG_UNUSED(dev);

	if (evt->type == UART_TX_DONE) {
		k_sem_give(&uart_tx_sem);
	}
}

/* ---------- Thread ---------- */
#define STACK_SIZE 4096
#define PRIORITY   7

static void fft_uart_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	if (!device_is_ready(uart_dev)) {
		printk("Error: USART3 nao pronto\n");
		return;
	}

	uart_callback_set(uart_dev, uart_cb, NULL);

	arm_rfft_fast_instance_f32 fft;

	arm_rfft_fast_init_f32(&fft, ADC_BUF_LEN);

	tx_frame.sync[0]     = 0xAA;
	tx_frame.sync[1]     = 0x55;
	tx_frame.sample_rate = DAC_FS_HZ;

	while (1) {
		k_sem_take(&adc_ready_sem, K_FOREVER);

		for (int i = 0; i < (int)ADC_BUF_LEN; i++) {
			fft_in[i] = (float32_t)adc_buf[i] - 2048.0f;
		}

		arm_rfft_fast_f32(&fft, fft_in, fft_out, 0);

		mag[0] = fabsf(fft_out[0]);
		arm_cmplx_mag_f32(&fft_out[2], &mag[1], FFT_BINS - 2);
		mag[FFT_BINS - 1] = fabsf(fft_out[1]);

		float32_t peak;
		uint32_t  peak_idx;

		arm_max_f32(mag, FFT_BINS, &peak, &peak_idx);
		if (peak < 1.0f) {
			peak = 1.0f;
		}
		for (int i = 0; i < FFT_BINS; i++) {
			tx_frame.data[i] = (uint16_t)(mag[i] / peak * 65535.0f);
		}

		int ret = uart_tx(uart_dev, (const uint8_t *)&tx_frame,
				  FRAME_TOTAL, SYS_FOREVER_US);
		if (ret == 0) {
			k_sem_take(&uart_tx_sem, K_FOREVER);
		} else {
			printk("Warning: uart_tx falhou: %d\n", ret);
		}

		k_msleep(100);
	}
}

K_THREAD_DEFINE(fft_uart_task_id, STACK_SIZE, fft_uart_entry, NULL, NULL, NULL,
		 PRIORITY, 0, 0);
