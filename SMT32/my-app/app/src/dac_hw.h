/* DAC1_CH1 + TIM6 via STM32 LL — saída senoide contínua via ISR (sem DMA). */
#pragma once

void dac_hw_init(void);

/* Troca a LUT usada pela ISR do TIM6 (chamado pela button_task).
 * Sincroniza através de atomic_t lut_sel em sync.h. */
void dac_hw_toggle_lut(void);
