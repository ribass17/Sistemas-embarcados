/*
 * Analisador de espectro em tempo real — NUCLEO-G474RE (Zephyr RTOS)
 *
 * Tarefas:
 *   fft_uart_task  — captura ADC → FFT → frame UART → ESP32
 *   led_task       — pisca LD2 a 1 Hz (referência de temporização)
 *   button_task    — alterna LUT do DAC pelo botão B1
 *   shell (builtin) — sysinfo {tasks|heap|rt} via lpuart1 / ST-LINK
 *
 * Hardware LL (sem driver Zephyr):
 *   TIM6  — ~44097 Hz, TRGO aciona DAC DMA e ADC
 *   DAC1_CH1 + DMA1_CH3 — saída senoide circular
 *   ADC2_IN17 + DMA1_CH1 — captura 512 amostras por frame
 *
 * Todos os objetos de sincronismo são Zephyr (k_sem, atomic_t).
 * Nenhum polling no código de aplicação.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "dac_hw.h"
#include "adc_hw.h"
#include "fft_uart.h"

/* Declarações das funções de init das tasks (sem headers próprios) */
extern void led_init(void);
extern void button_init(void);

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("=== Analisador de Espectro STM32G474RE ===");

	/* 1. Inicializa hardware LL (TIM6 + DAC DMA + ADC DMA).
	 *    Deve ser chamado após os drivers Zephyr (DAC1 pinctrl). */
	dac_hw_init();
	adc_hw_init();

	/* 2. Cria tarefas de aplicação */
	fft_uart_init();
	led_init();
	button_init();

	/* Shell é inicializado automaticamente pelo Zephyr (CONFIG_SHELL=y).
	 * main() pode retornar — o scheduler assume o controle. */
	return 0;
}
