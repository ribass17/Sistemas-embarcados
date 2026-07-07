# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Real-time audio spectrum analyzer on NUCLEO-G474RE (STM32G474RE, Cortex-M4F 170 MHz) using Zephyr RTOS 4.4.x. The STM32 generates a sine wave via DAC, samples it back via ADC, computes a 512-point FFT, and sends 518-byte frames to an ESP32 over USART3.

## Build & Flash

```bash
# Activate Zephyr venv first (required every session)
source ~/zephyrproject/.venv/bin/activate

# Build (from workspace root)
west build -p -b nucleo_g474re my-app/app   # -p = pristine (clean)
west build    -b nucleo_g474re my-app/app   # incremental

# Flash via ST-LINK (OpenOCD)
west flash --runner openocd

# Shell console (ST-LINK VCP)
tio /dev/ttyACM0 -b 115200       # Ctrl+T Q to quit
# or: sudo fuser -k /dev/ttyACM0  (if port busy)
```

## Architecture

### Signal chain
```
TIM6 @ 44097 Hz
  ├─→ update IRQ (TIM6_DAC_IRQn) → HAL_TIM_PeriodElapsedCallback() writes
  │     next sine sample via HAL_DAC_SetValue() → DAC1_CH1 → PA4  [dac_hw.c]
  └─→ TRGO=update              → ADC2_IN17 trigger ← PA4 (same pin, loopback) [adc_hw.c]
        └─→ DMA2_Channel1 (circular, 512 samples) → HAL_ADC_ConvCpltCallback()
              → k_sem_give(adc_ready_sem)
                └─→ fft_uart_task: arm_rfft_fast_f32 → USART3 DMA → ESP32
```

STM32Cube **HAL** is used for DAC/ADC/TIM6/GPIO (`app/Kconfig` selects
`APP_WITH_STM32_HAL` → `USE_STM32_HAL_{GPIO,ADC,ADC_EX,DAC,DAC_EX,DMA,
DMA_EX,CORTEX,TIM,TIM_EX}`). GPIO for the button/LED still goes through
Zephyr's own `zephyr/drivers/gpio.h` + devicetree, not HAL.

**DAC is CPU-driven via a TIM6 update interrupt, never DMA.** DAC+DMA on
this chip is a documented, reproducible failure — not a one-off board
defect. Confirmed independently in **two** different configurations:
LL + `DMA1_Channel3`, and later HAL + `DMA2_Channel2` (different DMA
controller, different channel, different API layer) — both hang the same
way: the DMA channel's `EN` bit never stays set and `CNDTR` never
decrements, so the DAC output stays frozen at 0. This matches ST's own
errata (ES0430, STM32G471/473/474/483/484) and multiple public reports of
the exact same symptom on this chip family (ST Community, the
`STM32CubeG4` GitHub repo, ChibiOS forum — see chat history for links).
The DAC DMA request is not queued: if a new trigger arrives before the
previous request is acknowledged, no new request is issued and the
channel gets stuck. Given DAC playback needs a request roughly every TIM6
period (~22.7 µs), this is essentially unavoidable at this configuration's
timing. **Do not reintroduce DMA for the DAC path** without a documented,
tested fix for this specific erratum.

Instead: `TIM6_DAC_IRQn` fires on every TIM6 update; `HAL_TIM_IRQHandler()`
dispatches to `HAL_TIM_PeriodElapsedCallback()` (implemented in
`dac_hw.c`), which writes the next sine sample straight into the DAC via
`HAL_DAC_SetValue()`. The DAC channel is configured with
`DAC_Trigger = DAC_TRIGGER_NONE`, so DHR→DOR transfers automatically
(~1 APB cycle) on every write — no DMA, no busy-polling thread, purely
interrupt-driven. TIM6's TRGO output is still needed — it's what triggers
the ADC's own conversion — so `HAL_TIMEx_MasterConfigSynchronization()`
(TRGO=UPDATE, MasterSlaveMode=ENABLE) stays in `tim6_init()` even though
the DAC no longer consumes TRGO itself.

ADC2's DMA (`DMA2_Channel1`) does **not** have this problem — it's been
solid since the beginning of this project, through both the LL and HAL
rewrites. It runs in **circular** mode: one `HAL_ADC_Start_DMA()` call at
init, then `HAL_ADC_ConvCpltCallback()` gives `adc_ready_sem` every time a
512-sample buffer completes. No per-frame restart needed.

### Tasks and priorities

Each task is self-starting via `K_THREAD_DEFINE` in its own file — no
`_init()` functions, no explicit thread creation from `main()`. `main()`
only brings up the shared DAC/ADC/TIM6 hardware before any task runs
(safe because the default `main` thread priority, 0, is higher than every
task's priority below, so `main()` always finishes first even though the
tasks are technically "ready" from boot).

| Thread | Priority | Stack | File |
|--------|---------|-------|------|
| button_task | 5 | 512 B | button.c |
| fft_uart_task | 7 | 4096 B | fft_uart.c |
| led_task | 10 | 512 B | led.c |
| shell (Zephyr) | ~14 | 2048 B | shell_cmds.c |

All inter-task synchronization uses Zephyr objects — no polling in application code.

### No header files, no shared sync.h — extern at point of use

Matching the professor's reference repo (`gustavowd/zephyr_app`), which has
zero `.h` files across its 4 source files: `dac_hw.c`, `adc_hw.c`,
`button.c`, `led.c`, `fft_uart.c`, `main.c` and `shell_cmds.c` have **no
header files of their own**. HAL handles (`hdac1`, `htim6`, `hadc2`,
`hdma_adc2`) are plain globals (no `static`), and whichever file needs a
symbol from another declares `extern` directly at the point of use instead
of including a shared header. Trade-off: the compiler doesn't check that an
`extern` declaration's signature matches the real definition elsewhere — a
mismatch would only surface at runtime, not compile time. Deliberate choice
for a small, fixed set of files; wouldn't scale to a larger project.

Cross-file kernel objects and their `extern` declarations:
- `adc_ready_sem` (`struct k_sem`, defined in `adc_hw.c`) — `extern`'d in
  `fft_uart.c`. Given by `HAL_ADC_ConvCpltCallback()`, taken by `fft_uart_task`.
- `sen_sel` (`atomic_t`, defined in `dac_hw.c`) — `extern`'d in `button.c`
  (read-only there, just for a log line). Written by `dac_hw_toggle_sen()`,
  read by the TIM6 ISR in `dac_hw.c` and by `button.c`.
- `uart_tx_sem` (`fft_uart.c`) and `btn_sem` (`button.c`) are **not**
  cross-file — each is given and taken entirely within its own file, so
  neither needs an `extern` anywhere else.

### No Zephyr logging subsystem — printk only

Matching the professor's repo (which uses `printk()` everywhere, and
`LOG_MODULE_DECLARE` only inside Zephyr's own `zbus` subsystem, never
`LOG_MODULE_REGISTER`), this project uses `printk()` for all diagnostic
output instead of `LOG_INF`/`LOG_ERR`/`LOG_MODULE_REGISTER`. `CONFIG_LOG`
is not set in `prj.conf`. Removing the logging subsystem also cut ~14 KB
of FLASH and ~3 KB of RAM compared to the `LOG_*` version.

### UART frame to ESP32 (USART3, PB10, 115200 baud, TX only)
```c
typedef struct __attribute__((packed)) {
    uint8_t  sync[2];      // 0xAA 0x55
    uint32_t sample_rate;  // 44100
    uint16_t data[256];    // magnitudes normalized to 0–65535
} fft_frame_t;             // 518 bytes total
```
Wire **PB10 (STM32 TX) → ESP32 RX** with a common GND. USART3 RX is not
used or configured — the ESP32 never talks back.

### DMA allocation (RM0440, DMAMUX1)
| Channel | DMAMUX canal | Request | Driver |
|---------|-------------|---------|--------|
| DMA1_CH4 | 3 | 0x1D USART3_TX | Zephyr |
| DMA2_Channel1 | 8 | 0x24 ADC2 | HAL (IRQ_CONNECT) |

DAC1_CH1 uses no DMA at all (see erratum note above) — it's driven by
`TIM6_DAC_IRQn` instead. DMA2 is used for ADC to avoid IRQ conflict:
Zephyr's DMA1 driver registers ISR for all DMA1 channels (IRQs 11–18) as
soon as `&dma1 { status = "okay"; }` is set (needed for USART3 TX). DMA2
is not in the device tree, so `IRQ_CONNECT(DMA2_Channel1_IRQn)` works
without conflict — this same reasoning is why the DAC's now-abandoned DMA
attempts also avoided `DMA1_Channel1` (the reference example this project
was compared against uses that channel, but it would collide with
Zephyr's DMA1 driver here).

### Key hardware notes
- **PA4** — analog mode, shared: DAC1_OUT1 output + ADC2_IN17 input (internal loopback, no wire)
- **DAC1 clock** — AHB2 bus (`__HAL_RCC_DAC1_CLK_ENABLE()`), not APB1
- **DAC+DMA erratum** — see signal chain note above; don't reintroduce DMA for the DAC path
- **ADC clock** must be set via `ADC_ClockPrescaler` **before** calibration runs internally in `HAL_ADC_Init()`, otherwise calibration hangs
- **DAC1 pinctrl** (PA4 → analog) is done manually via `HAL_GPIO_Init()` in `dac_hw.c` (Zephyr's DAC1 driver is disabled in the overlay)
- **Shell** is on `lpuart1` (PA2/PA3, ST-LINK VCP) — configured in board DTS, not the overlay
- `CONFIG_UART_INTERRUPT_DRIVEN=y` is required alongside `CONFIG_UART_ASYNC_API=y` for the shell backend to work
- **Never jumper anything to PA4** — it's the internal DAC/ADC analog node; connecting it to any other pin loads it down and reads as a near-zero flat signal instead of the sine wave

### Sine tables (project root)
- `dac_sen.h` — 512-sample sine, bin 100 ≈ 8613 Hz (user-provided), array `dac_sen[]`, `DAC_SEN_LEN 512`, `DAC_FS_HZ 44100`
- `dac_sen_alto.h` — 512-sample sine, bin 200 ≈ 17227 Hz (generated: `2048 + round(2047*sin(2π*200*i/512))`), array `dac_sen_alto[]`

Renamed from `dac_lut.h`/`dac_lut_high.h` — "LUT" was considered too jargon-heavy
for the presentation; `sen`/`sen_alto` (sine/high-sine) reads more directly.
`sen_sel` (`atomic_t`, in `dac_hw.c`) selects between the two: 0 = `dac_sen`
(bin 100), 1 = `dac_sen_alto` (bin 200).

Included via `target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)` in `app/CMakeLists.txt`.

## Shell commands

```
uart:~$ sysinfo tasks   # threads: name, priority, stack used/total
uart:~$ sysinfo heap    # kernel heap stats
uart:~$ sysinfo rt      # CPU cycles per thread
```

`k_thread_foreach_unlocked()` must be used (not `k_thread_foreach`) to avoid deadlock when called from shell context.
