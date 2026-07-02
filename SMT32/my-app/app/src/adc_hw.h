/* ADC2_IN17 (PA4, loopback do DAC) + DMA2_CH1 via STM32 LL.
 * Captura 512 amostras por requisição. Usa TIM6_TRGO como trigger.
 *
 * DMA2 (não gerenciado pelo driver Zephyr) permite IRQ_CONNECT sem
 * conflito com o driver de DMA1 do Zephyr. ISR dá k_sem_give(adc_ready_sem).
 */
#pragma once
#include <stdint.h>

#define ADC_BUF_LEN 512U

/* Buffer de amostras (uint16_t, 12 bits, range 0–4095). */
extern uint16_t adc_buf[ADC_BUF_LEN];

void adc_hw_init(void);

/* Inicia uma nova captura de ADC_BUF_LEN amostras.
 * Quando concluir, ISR DMA2_CH1 faz k_sem_give(&adc_ready_sem). */
void adc_hw_start_capture(void);
