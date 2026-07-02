/*
 * ADC2_IN17 (PA4) + DMA2_CH1 — captura de 512 amostras via TIM6_TRGO.
 *
 * Usa DMA2 (não DMA1) porque o driver Zephyr do DMA1 registra ISR para
 * todos os canais de DMA1. DMA2 não está no device tree, então
 * IRQ_CONNECT(DMA2_Channel1_IRQn) não gera conflito.
 *
 * DMAMUX1 mapeamento (STM32G474 RM0440):
 *   Canais 0-7  = DMA1_Channel1-8
 *   Canais 8-15 = DMA2_Channel1-8
 *   → DMA2_Channel1 usa DMAMUX1 canal 8, req 0x24 (ADC2)
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>

#include <stm32g4xx_ll_bus.h>
#include <stm32g4xx_ll_adc.h>
#include <stm32g4xx_ll_dma.h>
#include <stm32g4xx_ll_dmamux.h>

#include "sync.h"
#include "adc_hw.h"

LOG_MODULE_REGISTER(adc_hw, LOG_LEVEL_INF);

K_SEM_DEFINE(adc_ready_sem, 0, 1);

uint16_t adc_buf[ADC_BUF_LEN];

/* ---------- ISR DMA2_CH1 ---------- */
static void dma2_ch1_isr(void *arg)
{
	ARG_UNUSED(arg);
	if (LL_DMA_IsActiveFlag_TC1(DMA2)) {
		LL_DMA_ClearFlag_GI1(DMA2);
		k_sem_give(&adc_ready_sem);
	}
}

/* ---------- DMA2_CH1 para ADC2 ---------- */
static void adc_dma_init(void)
{
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);
	/* DMAMUX1 já está habilitado pelo driver Zephyr (via overlay) */

	/* DMAMUX1 canal 8 (= DMA2_CH1, 0-indexado) → req ADC2 */
	LL_DMAMUX_SetRequestID(DMAMUX1, 8U, LL_DMAMUX_REQ_ADC2);

	LL_DMA_ConfigTransfer(DMA2, LL_DMA_CHANNEL_1,
		LL_DMA_DIRECTION_PERIPH_TO_MEMORY |
		LL_DMA_MODE_NORMAL                 |
		LL_DMA_PERIPH_NOINCREMENT          |
		LL_DMA_MEMORY_INCREMENT            |
		LL_DMA_PDATAALIGN_HALFWORD         |
		LL_DMA_MDATAALIGN_HALFWORD         |
		LL_DMA_PRIORITY_VERYHIGH);

	LL_DMA_SetPeriphAddress(DMA2, LL_DMA_CHANNEL_1, (uint32_t)&ADC2->DR);
	LL_DMA_SetMemoryAddress(DMA2, LL_DMA_CHANNEL_1, (uint32_t)adc_buf);
	LL_DMA_SetDataLength(DMA2, LL_DMA_CHANNEL_1, ADC_BUF_LEN);
	LL_DMA_EnableIT_TC(DMA2, LL_DMA_CHANNEL_1);

	IRQ_CONNECT(DMA2_Channel1_IRQn, 5, dma2_ch1_isr, NULL, 0);
	irq_enable(DMA2_Channel1_IRQn);
}

/* ---------- ADC2 ---------- */
static void adc2_init(void)
{
	LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_ADC12);

	/* Clock deve ser configurado ANTES da calibração */
	LL_ADC_SetCommonClock(__LL_ADC_COMMON_INSTANCE(ADC2),
			      LL_ADC_CLOCK_SYNC_PCLK_DIV4);

	LL_ADC_DisableDeepPowerDown(ADC2);
	LL_ADC_EnableInternalRegulator(ADC2);
	k_busy_wait(20); /* t_ADCVREG_STUP = 20 µs */

	LL_ADC_StartCalibration(ADC2, LL_ADC_SINGLE_ENDED);
	while (LL_ADC_IsCalibrationOnGoing(ADC2)) {
	}

	LL_ADC_REG_SetSequencerLength(ADC2, LL_ADC_REG_SEQ_SCAN_DISABLE);
	LL_ADC_REG_SetSequencerRanks(ADC2, LL_ADC_REG_RANK_1,
				     LL_ADC_CHANNEL_17);
	LL_ADC_SetChannelSamplingTime(ADC2, LL_ADC_CHANNEL_17,
				      LL_ADC_SAMPLINGTIME_12CYCLES_5);
	LL_ADC_SetChannelSingleDiff(ADC2, LL_ADC_CHANNEL_17,
				    LL_ADC_SINGLE_ENDED);

	LL_ADC_REG_SetTriggerSource(ADC2, LL_ADC_REG_TRIG_EXT_TIM6_TRGO);
	LL_ADC_REG_SetTriggerEdge(ADC2, LL_ADC_REG_TRIG_EXT_RISING);
	/* UNLIMITED (DMNGT=11): DMAEN permanece ativo. Deve ser configurado
	 * com ADEN=0 para garantir que o CFGR não esteja protegido. */
	LL_ADC_REG_SetDMATransfer(ADC2, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);
	LL_ADC_SetResolution(ADC2, LL_ADC_RESOLUTION_12B);
	LL_ADC_REG_SetOverrun(ADC2, LL_ADC_REG_OVR_DATA_OVERWRITTEN);

	LL_ADC_Enable(ADC2);
	while (!LL_ADC_IsActiveFlag_ADRDY(ADC2)) {
	}

	/* Armar o ADC uma única vez — ADSTART permanece; cada TIM6_TRGO
	 * dispara uma conversão e gera DMA request (UNLIMITED mode). */
	LL_ADC_REG_StartConversion(ADC2);

	LOG_INF("ADC2 iniciado (PA4, trigger TIM6_TRGO, DMA2_CH1)");
}

/* ---------- API pública ---------- */

void adc_hw_init(void)
{
	adc_dma_init();
	adc2_init();
}

void adc_hw_start_capture(void)
{
	/* Apenas reinicia a DMA — ADC e DMAEN já estão ativos desde o init */
	LL_DMA_DisableChannel(DMA2, LL_DMA_CHANNEL_1);
	LL_DMA_ClearFlag_GI1(DMA2);
	LL_DMA_SetDataLength(DMA2, LL_DMA_CHANNEL_1, ADC_BUF_LEN);
	LL_DMA_EnableChannel(DMA2, LL_DMA_CHANNEL_1);
}
