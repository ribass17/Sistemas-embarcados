/*
 * ADC2_IN17 (PA4) + DMA2_Channel1 — captura de 512 amostras via TIM6_TRGO,

 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>

#include <stm32g4xx_hal.h>

#define ADC_BUF_LEN 512U

K_SEM_DEFINE(adc_ready_sem, 0, 1);

uint16_t adc_buf[ADC_BUF_LEN];

ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc2;

/* ---------- ISR DMA2_Channel1 ---------- */
static void dma2_channel1_adc_isr(void)
{
	HAL_DMA_IRQHandler(&hdma_adc2);
}

/* ---------- Callback HAL: buffer de captura completo ---------- */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
	ARG_UNUSED(hadc);
	k_sem_give(&adc_ready_sem);
}

/* ---------- DMA2_Channel1 para ADC2 ---------- */
static void adc_dma_init(void)
{
	__HAL_RCC_DMAMUX1_CLK_ENABLE();
	__HAL_RCC_DMA2_CLK_ENABLE();

	hdma_adc2.Instance                = DMA2_Channel1;
	hdma_adc2.Init.Request             = DMA_REQUEST_ADC2;
	hdma_adc2.Init.Direction           = DMA_PERIPH_TO_MEMORY;
	hdma_adc2.Init.PeriphInc           = DMA_PINC_DISABLE;
	hdma_adc2.Init.MemInc              = DMA_MINC_ENABLE;
	hdma_adc2.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
	hdma_adc2.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
	hdma_adc2.Init.Mode                = DMA_CIRCULAR;
	hdma_adc2.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
	HAL_DMA_Init(&hdma_adc2);

	__HAL_LINKDMA(&hadc2, DMA_Handle, hdma_adc2);

	IRQ_CONNECT(DMA2_Channel1_IRQn, 5, dma2_channel1_adc_isr, NULL, 0);
	irq_enable(DMA2_Channel1_IRQn);
}

/* ---------- ADC2 ---------- */
static void adc2_init(void)
{
	__HAL_RCC_ADC12_CLK_ENABLE();

	hadc2.Instance                   = ADC2;
	hadc2.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
	hadc2.Init.Resolution            = ADC_RESOLUTION_12B;
	hadc2.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
	hadc2.Init.GainCompensation      = 0;
	hadc2.Init.ScanConvMode          = ADC_SCAN_DISABLE;
	hadc2.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
	hadc2.Init.LowPowerAutoWait      = DISABLE;
	hadc2.Init.ContinuousConvMode    = DISABLE;
	hadc2.Init.NbrOfConversion       = 1;
	hadc2.Init.DiscontinuousConvMode = DISABLE;
	hadc2.Init.ExternalTrigConv      = ADC_EXTERNALTRIG_T6_TRGO;
	hadc2.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
	hadc2.Init.DMAContinuousRequests = ENABLE;
	hadc2.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
	hadc2.Init.OversamplingMode      = DISABLE;
	HAL_ADC_Init(&hadc2);

	ADC_ChannelConfTypeDef sConfig = {0};

	sConfig.Channel      = ADC_CHANNEL_17;
	sConfig.Rank         = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = ADC_SAMPLETIME_12CYCLES_5;
	sConfig.SingleDiff   = ADC_SINGLE_ENDED;
	sConfig.OffsetNumber = ADC_OFFSET_NONE;
	sConfig.Offset       = 0;
	HAL_ADC_ConfigChannel(&hadc2, &sConfig);

	printk("ADC2 iniciado via HAL (PA4, trigger TIM6_TRGO, DMA2_CH1)\n");
}

/* ---------- API pública ---------- */

void adc_hw_init(void)
{
	adc_dma_init();
	adc2_init();

	HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc_buf, ADC_BUF_LEN);
}
