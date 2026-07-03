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
  ├─→ update IRQ (TIM6_DAC_IRQn) → CPU writes DHR12R1 directly → DAC1_CH1 → PA4  [dac_hw.c]
  └─→ TRGO=update              → ADC2_IN17 trigger ← PA4 (same pin, loopback) [adc_hw.c]
        └─→ DMA2_CH1 (512 samples) → ISR → k_sem_give(adc_ready_sem)
              └─→ fft_uart_task: arm_rfft_fast_f32 → USART3 DMA → ESP32
```

**DAC is CPU-driven via ISR, not DMA.** DMA1_CH3 (circular, memory→DAC1_CH1)
was the original design but has a persistent transfer-error erratum on this
chip: DMA1_CH3 hits TE3, self-disables (CCR.EN clears), and never recovers
even with an explicit clear-and-rearm sequence — confirmed by reading the
registers live (`EN=0`, `TE3=1` stuck indefinitely). The fix was to drop
DMA entirely for the DAC path: `TIM6_DAC_IRQn` fires on every TIM6 update
and the ISR (`tim6_dac_isr` in dac_hw.c) writes the next LUT sample
straight into `DAC1->DHR12R1` via `LL_DAC_ConvertData12RightAligned()`.
With no external trigger enabled on the DAC channel, DHR→DOR transfers
automatically (~1 APB cycle) on each write. TIM6's TRGO output is still
needed — it's what triggers the ADC's own conversion — so
`LL_TIM_SetTriggerOutput`/`LL_TIM_EnableMasterSlaveMode` must stay in
`tim6_init()` even though the DAC no longer consumes TRGO itself.

### Tasks and priorities

| Thread | Priority | Stack | File |
|--------|---------|-------|------|
| button_task | 5 | 512 B | button.c |
| fft_uart_task | 7 | 4096 B | fft_uart.c |
| led_task | 10 | 512 B | led.c |
| shell (Zephyr) | ~14 | 2048 B | shell_cmds.c |

All inter-task synchronization uses Zephyr objects — no polling in application code.

### Shared sync objects (`sync.h`)
- `adc_ready_sem` — DMA2_CH1 ISR → fft_uart_task
- `uart_tx_sem`   — UART_TX_DONE callback → fft_uart_task
- `btn_sem`       — GPIO EXTI → button_task
- `lut_sel`       — atomic_t; 0=bin100 (8613 Hz), 1=bin200 (17227 Hz)

### UART frame to ESP32 (USART3, PB10, 115200 baud)
```c
typedef struct __attribute__((packed)) {
    uint8_t  sync[2];      // 0xAA 0x55
    uint32_t sample_rate;  // 44100
    uint16_t data[256];    // magnitudes normalized to 0–65535
} fft_frame_t;             // 518 bytes total
```

### DMA allocation (RM0440, DMAMUX1)
| Channel | DMAMUX canal | Request | Driver |
|---------|-------------|---------|--------|
| DMA1_CH4 | 3 | 0x1D USART3_TX | Zephyr |
| DMA2_CH1 | 8 | 0x24 ADC2 | LL (IRQ_CONNECT) |

DAC1_CH1 no longer uses DMA (see erratum note above) — it's driven by
`TIM6_DAC_IRQn` instead. DMA2 is used for ADC to avoid IRQ conflict:
Zephyr's DMA1 driver registers ISR for all DMA1 channels (IRQs 11–18).
DMA2 is not in the device tree, so `IRQ_CONNECT(DMA2_Channel1_IRQn)` works
without conflict.

### Key hardware notes
- **PA4** — analog mode, shared: DAC1_OUT1 output + ADC2_IN17 input (internal loopback, no wire)
- **DAC1 clock** — AHB2 bus (`LL_AHB2_GRP1_PERIPH_DAC1`), not APB1
- **DAC1_CH3 DMA erratum** — DMA1_CH3→DAC1_CH1 hits a persistent TE3 transfer error on this chip and never recovers; DAC is now CPU/ISR-driven instead (see signal chain note above). Don't reintroduce DMA for the DAC path without a hardware-level fix for this.
- **ADC clock** must be set via `LL_ADC_SetCommonClock()` **before** `LL_ADC_StartCalibration()`, otherwise calibration hangs
- **DAC1 pinctrl** (PA4 → analog) is done by the Zephyr DAC driver at POST_KERNEL before `main()`; `dac_hw_init()` only adds TIM6 + the update ISR
- **Shell** is on `lpuart1` (PA2/PA3, ST-LINK VCP) — configured in board DTS, not the overlay
- `CONFIG_UART_INTERRUPT_DRIVEN=y` is required alongside `CONFIG_UART_ASYNC_API=y` for the shell backend to work
- **UART loopback jumper for validation** — bridge **PB10↔PB11** (USART3 TX→RX) to let `fft_uart_task` receive and print its own frames (`LOOP RX: ...`). Never jumper anything to **PA4** — it's the internal DAC/ADC analog node; connecting it to PB11 (or anything else) loads it down and reads as a near-zero flat signal instead of the sine wave.

### LUT files (project root)
- `dac_lut.h` — 512-sample sine, bin 100 ≈ 8613 Hz (user-provided)
- `dac_lut_high.h` — 512-sample sine, bin 200 ≈ 17227 Hz (generated: `2048 + round(2047*sin(2π*200*i/512))`)

Included via `target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)` in `app/CMakeLists.txt`.

## Shell commands

```
uart:~$ sysinfo tasks   # threads: name, priority, stack used/total
uart:~$ sysinfo heap    # kernel heap stats
uart:~$ sysinfo rt      # CPU cycles per thread
```

`k_thread_foreach_unlocked()` must be used (not `k_thread_foreach`) to avoid deadlock when called from shell context.
