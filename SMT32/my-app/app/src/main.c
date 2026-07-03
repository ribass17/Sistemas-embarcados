/*
 * Analisador de espectro em tempo real — NUCLEO-G474RE (Zephyr RTOS)
 *
 * Tarefas (auto-iniciadas via K_THREAD_DEFINE, cada uma no seu arquivo):
 *   fft_uart_task  — captura ADC → FFT → frame UART → ESP32 (fft_uart.c)
 *   led_task       — pisca LD2 a 1 Hz (referência de temporização) (led.c)
 *   button_task    — alterna LUT do DAC pelo botão B1 (button.c)
 *   shell (builtin) — sysinfo {tasks|heap|rt} via lpuart1 / ST-LINK
 *
 * Hardware LL (sem driver Zephyr):
 *   TIM6  — ~44097 Hz, update aciona ISR do DAC e TRGO aciona ADC
 *   DAC1_CH1 — saída senoide via ISR do TIM6 (escrita direta no DHR,
 *              sem DMA — ver comentário no topo de dac_hw.c)
 *   ADC2_IN17 + DMA2_CH1 — captura 512 amostras por frame
 *
 * Todos os objetos de sincronismo são Zephyr (k_sem, atomic_t).
 * Nenhum polling no código de aplicação.
 *
 * main() só inicializa o hardware compartilhado (DAC/ADC/TIM6) antes de
 * qualquer task de aplicação rodar: a prioridade padrão da thread main
 * (0) é mais alta que as das tasks (5, 7, 10), então o scheduler sempre
 * termina main() primeiro, mesmo com as tasks já "prontas" desde o boot
 * via K_THREAD_DEFINE.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "dac_hw.h"
#include "adc_hw.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("=== Analisador de Espectro STM32G474RE ===");

	/* Inicializa hardware LL (TIM6 + DAC via ISR + ADC DMA).
	 * Deve ser chamado após os drivers Zephyr (DAC1 pinctrl). */
	dac_hw_init();
	adc_hw_init();

	/* Shell é inicializado automaticamente pelo Zephyr (CONFIG_SHELL=y). */
	return 0;
}
