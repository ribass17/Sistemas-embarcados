/*
 * led_task — pisca o LED LD2 (PA5, alias led0) a cada 500 ms.
 * Referência visual de temporização e corretude do scheduler.
 * Usa k_msleep (yield cooperativo, sem polling).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_task, LOG_LEVEL_INF);

#define LED_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

#define BLINK_MS 500U
#define STACK_SIZE 512
#define PRIORITY   10

static void led_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	if (!device_is_ready(led.port)) {
		LOG_ERR("GPIO do LED não pronto");
		return;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(BLINK_MS);
	}
}

K_THREAD_DEFINE(led_task_id, STACK_SIZE, led_entry, NULL, NULL, NULL,
		 PRIORITY, 0, 0);
