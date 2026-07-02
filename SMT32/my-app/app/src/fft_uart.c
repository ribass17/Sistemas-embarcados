/*
 * fft_uart_task: FFT 512 pts sobre ADC, envia frame 518 bytes.
 * Loopback: USART3 TX (PB10) → jumper → USART3 RX (PB11).
 * O frame recebido é parseado e o bin dominante impresso via shell.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <arm_math.h>

#include "sync.h"
#include "adc_hw.h"
#include "fft_uart.h"
#include "dac_lut.h"

LOG_MODULE_REGISTER(fft_uart, LOG_LEVEL_INF);

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

/* ---------- RX loopback — acumulador de bytes ---------- */
#define RX_DMA_LEN   64
#define RX_ACCUM_LEN 600

static uint8_t rx_dma[RX_DMA_LEN];
static uint8_t rx_accum[RX_ACCUM_LEN];
static uint32_t rx_pos;

static void rx_try_parse(void)
{
	/* Busca sync 0xAA 0x55 seguido de frame completo */
	for (uint32_t i = 0; i + FRAME_TOTAL <= rx_pos; i++) {
		if (rx_accum[i] != 0xAA || rx_accum[i + 1] != 0x55) {
			continue;
		}

		const fft_frame_t *f = (const fft_frame_t *)(rx_accum + i);
		uint32_t sr = f->sample_rate;

		/* Bin dominante (ignora DC bin 0) */
		uint16_t peak_val = 0;
		uint16_t peak_bin = 1;

		for (int b = 1; b < FFT_BINS; b++) {
			if (f->data[b] > peak_val) {
				peak_val = f->data[b];
				peak_bin = b;
			}
		}

		uint32_t freq_hz = (uint32_t)peak_bin * sr / (2U * FFT_BINS);

		printk("LOOP RX: sr=%u  bin=%u  freq=%u Hz  val=%u/65535\n",
		       sr, peak_bin, freq_hz, peak_val);

		/* Descarta bytes até depois deste frame */
		uint32_t consumed = i + FRAME_TOTAL;

		rx_pos -= consumed;
		memmove(rx_accum, rx_accum + consumed, rx_pos);
		return;
	}

	/* Buffer cheio sem sync — descarta para não travar */
	if (rx_pos >= RX_ACCUM_LEN) {
		rx_pos = 0;
	}
}

/* ---------- Callback UART ---------- */
static void uart_cb(const struct device *dev, struct uart_event *evt, void *ud)
{
	ARG_UNUSED(ud);

	switch (evt->type) {
	case UART_TX_DONE:
		k_sem_give(&uart_tx_sem);
		break;

	case UART_RX_RDY: {
		const uint8_t *src = evt->data.rx.buf + evt->data.rx.offset;
		uint32_t len = evt->data.rx.len;
		uint32_t copy = MIN(len, RX_ACCUM_LEN - rx_pos);

		memcpy(rx_accum + rx_pos, src, copy);
		rx_pos += copy;
		rx_try_parse();
		break;
	}

	case UART_RX_BUF_REQUEST:
		uart_rx_buf_rsp(dev, rx_dma, RX_DMA_LEN);
		break;

	case UART_RX_DISABLED:
		rx_pos = 0;
		uart_rx_enable(dev, rx_dma, RX_DMA_LEN, SYS_FOREVER_US);
		break;

	default:
		break;
	}
}

/* ---------- Thread ---------- */
#define STACK_SIZE 4096
#define PRIORITY   7

K_THREAD_STACK_DEFINE(fft_uart_stack, STACK_SIZE);
static struct k_thread fft_uart_thread;

static void fft_uart_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	arm_rfft_fast_instance_f32 fft;

	arm_rfft_fast_init_f32(&fft, ADC_BUF_LEN);

	tx_frame.sync[0]     = 0xAA;
	tx_frame.sync[1]     = 0x55;
	tx_frame.sample_rate = DAC_FS_HZ;

	while (1) {
		/* 1. Captura 512 amostras ADC via DMA2 */
		adc_hw_start_capture();
		k_sem_take(&adc_ready_sem, K_FOREVER);

		/* Diagnóstico: imprime primeiros 8 valores brutos uma vez */
		static bool dbg_printed;
		if (!dbg_printed) {
			dbg_printed = true;
			printk("ADC raw[0..7]: %u %u %u %u %u %u %u %u\n",
			       adc_buf[0], adc_buf[1], adc_buf[2], adc_buf[3],
			       adc_buf[4], adc_buf[5], adc_buf[6], adc_buf[7]);
		}

		/* 2. uint16 → float32, remove DC */
		for (int i = 0; i < (int)ADC_BUF_LEN; i++) {
			fft_in[i] = (float32_t)adc_buf[i] - 2048.0f;
		}

		/* 3. FFT 512 pontos */
		arm_rfft_fast_f32(&fft, fft_in, fft_out, 0);

		/* 4. Magnitudes — formato packed do rfft_fast:
		 *    out[0]=DC, out[1]=Nyquist, out[2k]/out[2k+1]=Re/Im bin k */
		mag[0] = fabsf(fft_out[0]);
		arm_cmplx_mag_f32(&fft_out[2], &mag[1], FFT_BINS - 2);
		mag[FFT_BINS - 1] = fabsf(fft_out[1]);

		/* 5. Normaliza pelo pico → uint16 */
		float32_t peak;
		uint32_t  peak_idx;

		arm_max_f32(mag, FFT_BINS, &peak, &peak_idx);
		if (peak < 1.0f) {
			peak = 1.0f;
		}
		for (int i = 0; i < FFT_BINS; i++) {
			tx_frame.data[i] = (uint16_t)(mag[i] / peak * 65535.0f);
		}

		/* 6. TX via USART3 DMA */
		int ret = uart_tx(uart_dev, (const uint8_t *)&tx_frame,
				  FRAME_TOTAL, SYS_FOREVER_US);
		if (ret == 0) {
			k_sem_take(&uart_tx_sem, K_FOREVER);
		} else {
			LOG_WRN("uart_tx falhou: %d", ret);
		}

		k_msleep(100);
	}
}

/* ---------- API pública ---------- */

void fft_uart_init(void)
{
	if (!device_is_ready(uart_dev)) {
		LOG_ERR("USART3 nao pronto");
		return;
	}

	uart_callback_set(uart_dev, uart_cb, NULL);
	uart_rx_enable(uart_dev, rx_dma, RX_DMA_LEN, SYS_FOREVER_US);

	k_thread_create(&fft_uart_thread, fft_uart_stack,
			K_THREAD_STACK_SIZEOF(fft_uart_stack),
			fft_uart_entry, NULL, NULL, NULL,
			PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&fft_uart_thread, "fft_uart");
}
