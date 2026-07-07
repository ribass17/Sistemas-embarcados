/*
 * DAC1 CH1 + TIM6 — saída senoide contínua via escrita direta no DHR,
 * disparada por interrupção.
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>

#include <stm32g4xx_hal.h>

#include "dac_sen.h"
#include "dac_sen_alto.h"

/* sen_sel: 0 → dac_sen, 1 → dac_sen_alto */
atomic_t sen_sel = ATOMIC_INIT(0);

TIM_HandleTypeDef htim6;
DAC_HandleTypeDef hdac1;
static uint32_t dac_isr_idx;

/* ---------- ISR TIM6 (update) — dispatcher HAL ---------- */
static void tim6_dac_isr(void *arg)
{
	ARG_UNUSED(arg);
	HAL_TIM_IRQHandler(&htim6);
}

/* Callback HAL chamado por HAL_TIM_IRQHandler a cada update do TIM6 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance != TIM6) {
		return;
	}

	const uint16_t *sen = (atomic_get(&sen_sel) == 0) ? dac_sen : dac_sen_alto;

	HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, sen[dac_isr_idx]);
	dac_isr_idx = (dac_isr_idx + 1) % DAC_SEN_LEN;
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

/* ---------- TIM6 ---------- */
static void tim6_init(void)
{
	__HAL_RCC_TIM6_CLK_ENABLE();

	htim6.Instance               = TIM6;
	htim6.Init.Prescaler         = 0;
	htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
	/* 170 MHz / (PSC+1) / (ARR+1) ≈ 44097 Hz  (erro < 0,01 %) */
	htim6.Init.Period            = 3854;
	htim6.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
	htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	HAL_TIM_Base_Init(&htim6);

	/* TRGO = update event → aciona o ADC (adc_hw.c usa TIM6_TRGO como
	 * trigger externo do ADC2). A escrita no DAC é feita pela ISR de
	 * update do próprio TIM6, não pelo TRGO. */
	TIM_MasterConfigTypeDef sMasterConfig = {0};

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
	sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_ENABLE;
	HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig);

	IRQ_CONNECT(TIM6_DAC_IRQn, 5, tim6_dac_isr, NULL, 0);
	irq_enable(TIM6_DAC_IRQn);
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
	/* Sem trigger de hardware: a CPU escreve o DHR direto na ISR do TIM6,
	 * e sem trigger a transferência DHR→DOR é automática (1 ciclo APB). */
	sConfig.DAC_Trigger                 = DAC_TRIGGER_NONE;
	sConfig.DAC_Trigger2                = DAC_TRIGGER_NONE;
	sConfig.DAC_OutputBuffer            = DAC_OUTPUTBUFFER_ENABLE;
	sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
	sConfig.DAC_UserTrimming            = DAC_TRIMMING_FACTORY;
	HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1);

	HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
}

/* ---------- API pública ---------- */

void dac_hw_init(void)
{
	pa4_analog_init();
	tim6_init();
	dac1_ch1_init();

	HAL_TIM_Base_Start_IT(&htim6);

	printk("DAC hw iniciado via HAL + ISR do TIM6 (sem DMA, sen bin %s)\n",
	       (atomic_get(&sen_sel) == 0) ? "100" : "200");
}

void dac_hw_toggle_sen(void)
{
	atomic_t new_sel = atomic_get(&sen_sel) ^ 1;

	atomic_set(&sen_sel, new_sel);

	printk("sen trocado -> bin %s (~%u Hz)\n",
	       (new_sel == 0) ? "100" : "200",
	       (new_sel == 0) ? 8613U : 17226U);
}
