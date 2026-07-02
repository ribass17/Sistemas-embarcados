/*
 * DAC1 CH1 + TIM6 + DMA1_CH3 — saída senoide contínua via circular DMA.
 *
 * Fluxo: TIM6 @ ~44097 Hz → TRGO → DAC1_CH1 trigger → DMA lê LUT e escreve
 *        em DAC->DHR12R1 → PA4 (analógico).
 *
 * Driver Zephyr do DAC1 está desabilitado no overlay.
 * Este módulo configura PA4 em modo analógico e inicializa DAC do zero via LL.
 *
 * DMA1_CH3 (LL index = LL_DMA_CHANNEL_3) ↔ DMAMUX1 canal 2, req 0x06.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stm32g4xx_ll_bus.h>
#include <stm32g4xx_ll_gpio.h>
#include <stm32g4xx_ll_tim.h>
#include <stm32g4xx_ll_dac.h>
#include <stm32g4xx_ll_dma.h>
#include <stm32g4xx_ll_dmamux.h>

#include "sync.h"
#include "dac_hw.h"
#include "dac_lut.h"
#include "dac_lut_high.h"

LOG_MODULE_REGISTER(dac_hw, LOG_LEVEL_INF);

/* lut_sel: 0 → dac_lut, 1 → dac_lut_high */
atomic_t lut_sel = ATOMIC_INIT(0);

/* ---------- TIM6 ---------- */
static void tim6_init(void)
{
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM6);

	/* 170 MHz / (PSC+1) / (ARR+1) ≈ 44097 Hz  (erro < 0,01 %) */
	LL_TIM_SetPrescaler(TIM6, 0);
	LL_TIM_SetAutoReload(TIM6, 3854U);

	/* TRGO = update event → aciona DAC e ADC */
	LL_TIM_SetTriggerOutput(TIM6, LL_TIM_TRGO_UPDATE);
	LL_TIM_EnableMasterSlaveMode(TIM6);
}

/* ---------- DMA1_CH3 para DAC1_CH1 ---------- */
static void dac_dma_init(const uint16_t *lut)
{
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

	/* DMAMUX1 canal 2 (= DMA1_CH3 0-indexado) → req DAC1_CH1 */
	LL_DMAMUX_SetRequestID(DMAMUX1, 2U, LL_DMAMUX_REQ_DAC1_CH1);

	LL_DMA_ConfigTransfer(DMA1, LL_DMA_CHANNEL_3,
		LL_DMA_DIRECTION_MEMORY_TO_PERIPH |
		LL_DMA_MODE_CIRCULAR               |
		LL_DMA_PERIPH_NOINCREMENT          |
		LL_DMA_MEMORY_INCREMENT            |
		LL_DMA_PDATAALIGN_HALFWORD         |
		LL_DMA_MDATAALIGN_HALFWORD         |
		LL_DMA_PRIORITY_HIGH);

	LL_DMA_SetPeriphAddress(DMA1, LL_DMA_CHANNEL_3,
				LL_DAC_DMA_GetRegAddr(DAC1, LL_DAC_CHANNEL_1,
						      LL_DAC_DMA_REG_DATA_12BITS_RIGHT_ALIGNED));
	LL_DMA_SetMemoryAddress(DMA1, LL_DMA_CHANNEL_3, (uint32_t)lut);
	LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_3, DAC_LUT_LEN);
	LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_3);
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

	/* Garantir canal desabilitado antes de configurar TSEL */
	LL_DAC_Disable(DAC1, LL_DAC_CHANNEL_1);

	/* MODE1=0 → saída normal com buffer habilitado (default após reset) */
	LL_DAC_SetOutputMode(DAC1, LL_DAC_CHANNEL_1, LL_DAC_OUTPUT_MODE_NORMAL);
	LL_DAC_SetOutputBuffer(DAC1, LL_DAC_CHANNEL_1, LL_DAC_OUTPUT_BUFFER_ENABLE);
	LL_DAC_SetOutputConnection(DAC1, LL_DAC_CHANNEL_1, LL_DAC_OUTPUT_CONNECT_GPIO);

	LL_DAC_SetTriggerSource(DAC1, LL_DAC_CHANNEL_1, LL_DAC_TRIG_EXT_TIM6_TRGO);
	LL_DAC_EnableTrigger(DAC1, LL_DAC_CHANNEL_1);
	LL_DAC_EnableDMAReq(DAC1, LL_DAC_CHANNEL_1);

	LL_DAC_Enable(DAC1, LL_DAC_CHANNEL_1);
	k_busy_wait(10); /* t_wakeup ~8 µs */
}

/* ---------- API pública ---------- */

void dac_hw_init(void)
{
	const uint16_t *lut = (atomic_get(&lut_sel) == 0) ? dac_lut : dac_lut_high;

	pa4_analog_init();
	tim6_init();
	dac_dma_init(lut);
	dac1_ch1_init();

	LL_TIM_EnableCounter(TIM6);

	/* Diagnóstico: após 2ms (>88 ciclos TIM6), DAC deve mostrar valor da LUT */
	k_busy_wait(2000);
	uint32_t dor1 = DAC1->DOR1 & 0xFFFU;
	uint32_t cr   = DAC1->CR;
	printk("DAC CR=0x%08x  DOR1=%u\n", cr, dor1);
	/* CR esperado: EN1=1(b0) TEN1=1(b2) TSEL1=8(b3..1=0b1000→bits4:1) DMAEN1=1(b12)
	 * DOR1 esperado: valor da LUT (~2048), muda a cada print se TIM6 rodando */

	LOG_INF("DAC hw iniciado (LUT bin %s)",
		(atomic_get(&lut_sel) == 0) ? "100" : "200");
}

void dac_hw_toggle_lut(void)
{
	atomic_t new_sel = atomic_get(&lut_sel) ^ 1;
	atomic_set(&lut_sel, new_sel);

	const uint16_t *lut = (new_sel == 0) ? dac_lut : dac_lut_high;

	/* Desabilitar canal DMA, atualizar endereço, reabilitar */
	LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_3);
	while (LL_DMA_IsEnabledChannel(DMA1, LL_DMA_CHANNEL_3)) {
	}
	LL_DMA_SetMemoryAddress(DMA1, LL_DMA_CHANNEL_3, (uint32_t)lut);
	LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_3, DAC_LUT_LEN);
	LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_3);

	LOG_INF("LUT trocada → bin %s (~%u Hz)",
		(new_sel == 0) ? "100 (8613" : "200 (17226", 0U);
}
