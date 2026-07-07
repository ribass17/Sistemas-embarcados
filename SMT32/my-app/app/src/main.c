#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

extern void dac_hw_init(void);
extern void adc_hw_init(void);

int main(void)
{
	dac_hw_init();
	adc_hw_init();
	return 0;
}
