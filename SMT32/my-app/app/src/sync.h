/* Objetos de sincronismo compartilhados entre tarefas. */
#pragma once
#include <zephyr/kernel.h>

/* ADC DMA2_CH1 "transfer complete" ISR → fft_uart_task */
extern struct k_sem adc_ready_sem;

/* UART TX_DONE callback → fft_uart_task */
extern struct k_sem uart_tx_sem;

/* GPIO EXTI botão → button_task */
extern struct k_sem btn_sem;

/* Seleção de LUT: 0 = dac_lut (bin 100), 1 = dac_lut_high (bin 200) */
extern atomic_t lut_sel;
