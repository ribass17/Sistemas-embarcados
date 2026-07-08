/*
 * led_hw_init — pisca o LED LD2 a cada 500 ms via k_timer periódico.
 * Sem thread dedicada: como o professor faz no ex_freertos_g474 (timer de
 * software puro), não vale a pena gastar uma thread inteira só pra isso.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define LED_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

#define BLINK_MS 500U

static void led_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	gpio_pin_toggle_dt(&led);
}

K_TIMER_DEFINE(led_blink_timer, led_expiry, NULL);

void led_hw_init(void)
{
	if (!device_is_ready(led.port)) {
		printk("Error: GPIO do LED não pronto\n");
		return;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	k_timer_start(&led_blink_timer, K_MSEC(BLINK_MS), K_MSEC(BLINK_MS));
}
