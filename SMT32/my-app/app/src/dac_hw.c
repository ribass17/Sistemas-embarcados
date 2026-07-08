/*
 * DAC1 CH1 + TIM7 + DMA2_Channel2 — saída senoide contínua via DMA circular,
 * disparada por trigger de hardware (TRGO) dedicado, independente do TIM6
 * que dispara o ADC (adc_hw.c). Ver CLAUDE.md para o histórico da errata
 * ES0430 e por que os dois periféricos não podem compartilhar o mesmo TRGO.
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>

#include <stm32g4xx_hal.h>

#include "dac_sen.h"
#include "dac_sen_alto.h"

atomic_t sen_sel = ATOMIC_INIT(0);

TIM_HandleTypeDef htim6;
TIM_HandleTypeDef htim7;
DAC_HandleTypeDef hdac1;
DMA_HandleTypeDef hdma_dac1_ch1;

/* ---------- ISR DMA2_Channel2 (DAC1 CH1) — dispatcher HAL ---------- */
static void dma2_channel2_dac_isr(void)
{
	HAL_DMA_IRQHandler(&hdma_dac1_ch1);
}

/* ---------- PA4 modo analógico ---------- */
static void pa4_analog_init(void)
{
	__HAL_RCC_GPIOA_CLK_ENABLE();

	GPIO_InitTypeDef gpio = {0};

	gpio.Pin  = GPIO_PIN_4;
	gpio.Mode = GPIO_MODE_ANALOG;
	gpio.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOA, &gpio);
}

/* ---------- TIM6 — TRGO consumido só pelo ADC2 (adc_hw.c) ---------- */
static void tim6_init(void)
{
	__HAL_RCC_TIM6_CLK_ENABLE();

	htim6.Instance               = TIM6;
	htim6.Init.Prescaler         = 0;
	htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
	htim6.Init.Period            = 3854;
	htim6.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
	htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	HAL_TIM_Base_Init(&htim6);

	TIM_MasterConfigTypeDef sMasterConfig = {0};

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
	sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_ENABLE;
	HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig);
}

/* ---------- TIM7 — TRGO dedicado ao DAC1 CH1, mesma taxa do TIM6 ---------- */
static void tim7_init(void)
{
	__HAL_RCC_TIM7_CLK_ENABLE();

	htim7.Instance               = TIM7;
	htim7.Init.Prescaler         = 0;
	htim7.Init.CounterMode       = TIM_COUNTERMODE_UP;
	htim7.Init.Period            = 3854;
	htim7.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
	htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	HAL_TIM_Base_Init(&htim7);

	TIM_MasterConfigTypeDef sMasterConfig = {0};

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
	sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_ENABLE;
	HAL_TIMEx_MasterConfigSynchronization(&htim7, &sMasterConfig);
}

/* ---------- DMA2_Channel2 para DAC1 CH1 ---------- */
static void dac_dma_init(void)
{
	__HAL_RCC_DMAMUX1_CLK_ENABLE();
	__HAL_RCC_DMA2_CLK_ENABLE();

	hdma_dac1_ch1.Instance                = DMA2_Channel2;
	hdma_dac1_ch1.Init.Request             = DMA_REQUEST_DAC1_CHANNEL1;
	hdma_dac1_ch1.Init.Direction           = DMA_MEMORY_TO_PERIPH;
	hdma_dac1_ch1.Init.PeriphInc           = DMA_PINC_DISABLE;
	hdma_dac1_ch1.Init.MemInc              = DMA_MINC_ENABLE;
	hdma_dac1_ch1.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
	hdma_dac1_ch1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
	hdma_dac1_ch1.Init.Mode                = DMA_CIRCULAR;
	hdma_dac1_ch1.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
	HAL_DMA_Init(&hdma_dac1_ch1);

	__HAL_LINKDMA(&hdac1, DMA_Handle1, hdma_dac1_ch1);

	IRQ_CONNECT(DMA2_Channel2_IRQn, 5, dma2_channel2_dac_isr, NULL, 0);
	irq_enable(DMA2_Channel2_IRQn);
}

/* ---------- DAC1 CH1 ---------- */
static void dac1_ch1_init(void)
{
	__HAL_RCC_DAC1_CLK_ENABLE();

	hdac1.Instance = DAC1;
	HAL_DAC_Init(&hdac1);

	DAC_ChannelConfTypeDef sConfig = {0};

	sConfig.DAC_HighFrequency          = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
	sConfig.DAC_DMADoubleDataMode       = DISABLE;
	sConfig.DAC_SignedFormat            = DISABLE;
	sConfig.DAC_SampleAndHold           = DAC_SAMPLEANDHOLD_DISABLE;
	sConfig.DAC_Trigger                 = DAC_TRIGGER_T7_TRGO;
	sConfig.DAC_Trigger2                = DAC_TRIGGER_NONE;
	sConfig.DAC_OutputBuffer            = DAC_OUTPUTBUFFER_ENABLE;
	sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
	sConfig.DAC_UserTrimming            = DAC_TRIMMING_FACTORY;
	HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1);
}

/* ---------- API pública ---------- */

void dac_hw_init(void)
{
	pa4_analog_init();
	tim6_init();
	tim7_init();
	dac_dma_init();
	dac1_ch1_init();

	/* Ordem crítica: arma o DMA do DAC ANTES de iniciar o TIM7, para que o
	 * primeiro TRGO já encontre um pedido de DMA pronto para ser atendido
	 * (é essa a lição do padrão validado em gustavowd/ex_freertos_g474
	 * para evitar a errata ES0430 — ver nota no CLAUDE.md).
	 */
	HAL_TIM_Base_Start(&htim6);
	HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)dac_sen, DAC_SEN_LEN, DAC_ALIGN_12B_R);
	HAL_TIM_Base_Start(&htim7);

	printk("DAC hw iniciado via HAL + DMA2_Channel2 (TIM7 TRGO dedicado, sen bin %s)\n",
	       (atomic_get(&sen_sel) == 0) ? "100" : "200");
}

void dac_hw_toggle_sen(void)
{
	atomic_t new_sel = atomic_get(&sen_sel) ^ 1;

	atomic_set(&sen_sel, new_sel);

	const uint16_t *sen = (new_sel == 0) ? dac_sen : dac_sen_alto;

	HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
	HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)sen, DAC_SEN_LEN, DAC_ALIGN_12B_R);

	printk("sen trocado -> bin %s (~%u Hz)\n",
	       (new_sel == 0) ? "100" : "200",
	       (new_sel == 0) ? 8613U : 17226U);
}
