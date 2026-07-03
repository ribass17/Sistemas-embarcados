/*
 * DAC1 CH1 + TIM6 — saída senoide contínua via escrita direta no DHR.
 *
 * Fluxo: TIM6 @ ~44097 Hz → update → ISR TIM6 escreve o próximo valor da
 *        LUT em DAC->DHR12R1 → transferência automática para DOR1 → PA4.
 *
 * O caminho original (DMA1_CH3 + trigger TIM6_TRGO no DAC) sofre de um
 * erratum persistente neste chip: o canal de DMA cai em erro de
 * transferência (TE3) e se autodesabilita, sem se recuperar — confirmado
 * lendo os registradores ao vivo (CCR.EN volta a 0, TE3 fica setado
 * indefinidamente). Por isso a geração do DAC é feita por CPU via ISR do
 * TIM6, sem DMA e sem trigger de hardware no DAC.
 *
 * Driver Zephyr do DAC1 está desabilitado no overlay.
 * Este módulo configura PA4 em modo analógico e inicializa DAC do zero via LL.
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>

#include <stm32g4xx_ll_bus.h>
#include <stm32g4xx_ll_gpio.h>
#include <stm32g4xx_ll_tim.h>
#include <stm32g4xx_ll_dac.h>

#include "sync.h"
#include "dac_hw.h"
#include "dac_lut.h"
#include "dac_lut_high.h"

LOG_MODULE_REGISTER(dac_hw, LOG_LEVEL_INF);

/* lut_sel: 0 → dac_lut, 1 → dac_lut_high */
atomic_t lut_sel = ATOMIC_INIT(0);

static uint32_t dac_isr_idx;

/* ---------- TIM6 ---------- */
static void tim6_init(void)
{
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM6);

	/* 170 MHz / (PSC+1) / (ARR+1) ≈ 44097 Hz  (erro < 0,01 %) */
	LL_TIM_SetPrescaler(TIM6, 0);
	LL_TIM_SetAutoReload(TIM6, 3854U);

	/* TRGO = update event → aciona o ADC (adc_hw.c usa TIM6_TRGO como
	 * trigger externo do ADC2). A escrita no DAC agora é feita pela ISR
	 * de update do próprio TIM6, não pelo TRGO. */
	LL_TIM_SetTriggerOutput(TIM6, LL_TIM_TRGO_UPDATE);
	LL_TIM_EnableMasterSlaveMode(TIM6);
}

/* ---------- ISR TIM6 (update) — escreve o DHR direto, sem DMA ---------- */
static void tim6_dac_isr(void *arg)
{
	ARG_UNUSED(arg);

	if (LL_TIM_IsActiveFlag_UPDATE(TIM6)) {
		LL_TIM_ClearFlag_UPDATE(TIM6);

		const uint16_t *lut = (atomic_get(&lut_sel) == 0) ? dac_lut : dac_lut_high;

		LL_DAC_ConvertData12RightAligned(DAC1, LL_DAC_CHANNEL_1, lut[dac_isr_idx]);
		dac_isr_idx = (dac_isr_idx + 1) % DAC_LUT_LEN;
	}
}

/* ---------- PA4 modo analógico ---------- */
static void pa4_analog_init(void)
{
	LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_4, LL_GPIO_MODE_ANALOG);
}

/* ---------- DAC1 CH1 — init completo via LL (driver Zephyr desabilitado) ---------- */
static void dac1_ch1_init(void)
{
	LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_DAC1);

	LL_DAC_Disable(DAC1, LL_DAC_CHANNEL_1);

	/* MODE1=0 → saída normal com buffer habilitado (default após reset) */
	LL_DAC_SetOutputMode(DAC1, LL_DAC_CHANNEL_1, LL_DAC_OUTPUT_MODE_NORMAL);
	LL_DAC_SetOutputBuffer(DAC1, LL_DAC_CHANNEL_1, LL_DAC_OUTPUT_BUFFER_ENABLE);
	LL_DAC_SetOutputConnection(DAC1, LL_DAC_CHANNEL_1, LL_DAC_OUTPUT_CONNECT_GPIO);

	/* Sem trigger de hardware: a CPU escreve o DHR direto na ISR do TIM6,
	 * e sem trigger a transferência DHR→DOR é automática (1 ciclo APB). */
	LL_DAC_Enable(DAC1, LL_DAC_CHANNEL_1);
	k_busy_wait(10); /* t_wakeup ~8 µs */
}

/* ---------- API pública ---------- */

void dac_hw_init(void)
{
	pa4_analog_init();
	tim6_init();
	dac1_ch1_init();

	LL_TIM_ClearFlag_UPDATE(TIM6);
	IRQ_CONNECT(TIM6_DAC_IRQn, 5, tim6_dac_isr, NULL, 0);
	irq_enable(TIM6_DAC_IRQn);
	LL_TIM_EnableIT_UPDATE(TIM6);

	LL_TIM_EnableCounter(TIM6);

	LOG_INF("DAC hw iniciado via ISR TIM6 (LUT bin %s)",
		(atomic_get(&lut_sel) == 0) ? "100" : "200");
}

void dac_hw_toggle_lut(void)
{
	atomic_t new_sel = atomic_get(&lut_sel) ^ 1;

	atomic_set(&lut_sel, new_sel);

	LOG_INF("LUT trocada → bin %s (~%u Hz)",
		(new_sel == 0) ? "100 (8613" : "200 (17226", 0U);
}
