/*
 * button_task — detecta o pressionamento do botão B1 (PC13, alias sw0)
 * e alterna a LUT do DAC entre bin 100 (~8613 Hz) e bin 200 (~17227 Hz).
 *
 * Sem polling: GPIO interrupt → k_sem_give(btn_sem) → button_task k_sem_take.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "sync.h"
#include "dac_hw.h"

LOG_MODULE_REGISTER(btn_task, LOG_LEVEL_INF);

#define BTN_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(BTN_NODE, gpios);
static struct gpio_callback btn_gpio_cb;

K_SEM_DEFINE(btn_sem, 0, 1);

#define STACK_SIZE 512
#define PRIORITY   5

/* ISR: chamada pelo subsistema GPIO na borda ativa do botão */
static void btn_isr(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	k_sem_give(&btn_sem);
}

static void btn_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	if (!device_is_ready(btn.port)) {
		LOG_ERR("GPIO do botão não pronto");
		return;
	}

	gpio_pin_configure_dt(&btn, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&btn_gpio_cb, btn_isr, BIT(btn.pin));
	gpio_add_callback(btn.port, &btn_gpio_cb);

	while (1) {
		/* Bloqueia sem consumir CPU até o próximo clique */
		k_sem_take(&btn_sem, K_FOREVER);

		/* Debounce simples: ignorar eventos seguidos em < 200 ms */
		k_msleep(200);
		k_sem_reset(&btn_sem); /* descartar eventos acumulados */

		dac_hw_toggle_lut();
		LOG_INF("Botão pressionado → LUT %s",
			(atomic_get(&lut_sel) == 0) ? "bin 100" : "bin 200");
	}
}

K_THREAD_DEFINE(button_task_id, STACK_SIZE, btn_entry, NULL, NULL, NULL,
		 PRIORITY, 0, 0);
