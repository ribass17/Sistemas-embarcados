/* DAC1_CH1 + TIM6 + DMA1_CH3 via STM32 LL (saída senoide contínua, circular). */
#pragma once

void dac_hw_init(void);

/* Troca o buffer DMA para a outra LUT (chamado pela button_task).
 * Sincroniza através de atomic_t lut_sel em sync.h. */
void dac_hw_toggle_lut(void);
