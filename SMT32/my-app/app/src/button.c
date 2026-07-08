/*
 * button_task — detecta o pressionamento do botão B1 e alterna a senoide do
 *DAC entre bin 100 (~8613 Hz) e bin 200 (~17227 Hz).
 *
 * Debounce via k_timer retriggerable: cada borda reinicia um one-shot de
 * 100 ms; só quando o timer expira de fato (100 ms sem nova borda) é que o
 * pino é relido e, se ainda ativo, o evento é sinalizado pra thread.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

extern void dac_hw_toggle_sen(void);
extern atomic_t sen_sel; /* definido em dac_hw.c; só lido aqui p/ log */

#define BTN_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(BTN_NODE, gpios);
static struct gpio_callback btn_gpio_cb;

K_SEM_DEFINE(btn_sem, 0, 1);

#define DEBOUNCE_MS 100
#define STACK_SIZE  512
#define PRIORITY    5

static void btn_debounce_expiry(struct k_timer *timer);
K_TIMER_DEFINE(btn_debounce_timer, btn_debounce_expiry, NULL);

/* ISR: chamada pelo subsistema GPIO na borda ativa do botão */
static void btn_isr(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	/* (Re)inicia o one-shot; repiques dentro da janela só adiam a checagem */
	k_timer_start(&btn_debounce_timer, K_MSEC(DEBOUNCE_MS), K_NO_WAIT);
}

/* Timer expirou sem nova borda por DEBOUNCE_MS: pressionamento estabilizado */
static void btn_debounce_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	if (gpio_pin_get_dt(&btn)) {
		k_sem_give(&btn_sem);
	}
}

static void btn_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	if (!device_is_ready(btn.port)) {
		printk("Error: GPIO do botão não pronto\n");
		return;
	}

	gpio_pin_configure_dt(&btn, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&btn_gpio_cb, btn_isr, BIT(btn.pin));
	gpio_add_callback(btn.port, &btn_gpio_cb);

	while (1) {
		/* Bloqueia sem consumir CPU até o próximo clique debounced */
		k_sem_take(&btn_sem, K_FOREVER);

		dac_hw_toggle_sen();
		printk("Botão pressionado -> seno %s\n",
		       (atomic_get(&sen_sel) == 0) ? "bin 100" : "bin 200");
	}
}

K_THREAD_DEFINE(button_task_id, STACK_SIZE, btn_entry, NULL, NULL, NULL,
		 PRIORITY, 0, 0);
