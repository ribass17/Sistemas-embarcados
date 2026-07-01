# Protocolo UART: STM32 → ESP32

## Visão Geral

O ESP32 aguarda frames binários de tamanho fixo enviados pelo STM32 via UART.
Cada frame carrega o resultado de uma janela FFT (256 bins de magnitude).

```
STM32 (Zephyr) ──UART 115200 8N1──> ESP32 ──WebSocket──> Browser
```

---

## Configuração UART

| Parâmetro    | Valor   |
|-------------|---------|
| Baud rate   | 115200  |
| Data bits   | 8       |
| Parity      | None    |
| Stop bits   | 1       |
| Flow control| None    |
| Direção     | TX only (STM32 envia, ESP32 recebe) |

GPIO no ESP32: **RX = GPIO 16**, TX = GPIO 17 (não usado).

---

## Formato do Frame

Tamanho fixo: **518 bytes**, little-endian, sem CRC.

```
Offset  Tamanho  Tipo       Campo         Descrição
──────  ───────  ─────────  ────────────  ──────────────────────────────
  0       1      uint8_t    sync[0]       Sempre 0xAA
  1       1      uint8_t    sync[1]       Sempre 0x55
  2       4      uint32_t   sample_rate   Frequência de amostragem em Hz
  6     512      uint16_t   data[256]     Magnitudes FFT (0–65535)
──────────────────────────────────────────────────────────────────────
                             Total        518 bytes
```

O ESP32 detecta o início do frame pelos bytes de sincronismo `0xAA 0x55`
e lê os 516 bytes seguintes em bloco.

---

## Struct C (copiar para o STM32)

```c
#define FFT_BINS    256
#define FRAME_TOTAL (2 + 4 + FFT_BINS * 2)  /* 518 bytes */

typedef struct __attribute__((packed)) {
    uint8_t  sync[2];          /* {0xAA, 0x55} — não alterar */
    uint32_t sample_rate;      /* Hz do ADC, ex: 44100       */
    uint16_t data[FFT_BINS];   /* magnitudes, little-endian  */
} fft_frame_t;
```

> `__attribute__((packed))` garante que não há padding. Verificar se o
> compilador ARM do Zephyr (GCC) honra isso — normalmente sim.

---

## Preenchimento dos Campos

### `sync`
Fixo. Nunca alterar.
```c
frame.sync[0] = 0xAA;
frame.sync[1] = 0x55;
```

### `sample_rate`
Frequência real com que o ADC do STM32 amostrou o sinal.
```c
frame.sample_rate = 44100;  /* ou o valor real do clock do ADC */
```
O browser usa este valor para calcular o eixo X em Hz:
```
frequência do bin i = i × sample_rate / (2 × FFT_BINS)
```

### `data[256]`
Magnitudes da FFT normalizadas para `uint16_t` (0–65535).
A escala é livre — o browser exibe o valor bruto no eixo Y.

```c
for (int i = 0; i < FFT_BINS; i++) {
    float mag = cabsf(fft_output[i]);   /* ou equivalente */
    float norm = mag / max_magnitude;   /* normaliza 0.0–1.0 */
    frame.data[i] = (uint16_t)(norm * 65535.0f);
}
```

---

## Envio (Zephyr)

### Polling (simples, bloqueia a thread)
```c
const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

void fft_frame_send(const fft_frame_t *frame)
{
    const uint8_t *buf = (const uint8_t *)frame;
    for (size_t i = 0; i < FRAME_TOTAL; i++)
        uart_poll_out(uart, buf[i]);
}
```

### Async / DMA (não bloqueia)
```c
/* prj.conf: CONFIG_UART_ASYNC_API=y */

static void uart_cb(const struct device *dev, struct uart_event *evt, void *ud)
{
    /* UART_EVT_TX_DONE: frame transmitido, liberar buffer se necessário */
}

void fft_uart_init(void)
{
    uart_callback_set(uart, uart_cb, NULL);
}

void fft_frame_send(const fft_frame_t *frame)
{
    uart_tx(uart, (const uint8_t *)frame, FRAME_TOTAL, SYS_FOREVER_US);
}
```

### Device Tree (`app.overlay`)
```dts
&usart1 {
    status = "okay";
    current-speed = <115200>;
};
```

---

## Cadência de Envio

```
115200 baud ÷ 10 bits/byte = 11.520 bytes/s
518 bytes/frame → tempo mínimo = 45 ms/frame → máx ≈ 22 frames/s
```

Enviar a cada **100 ms (~10 fps)** é suficiente para visualização em tempo real
e mantém a UART com folga.

```c
while (1) {
    fft_compute(&frame);   /* preenche frame.data[] */
    fft_frame_send(&frame);
    k_msleep(100);
}
```

---

## Detecção de Erros no ESP32

O ESP32 **não usa CRC**. As únicas verificações são:

| Condição | Ação do ESP32 |
|----------|---------------|
| Sync não encontrado | descarta bytes até achar `0xAA 0x55` |
| Leitura curta (timeout 500 ms) | descarta o frame, loga warning |
| UART overflow | flush do buffer, reinicia busca de sync |

Para robustez, o STM32 deve garantir que os 518 bytes sejam enviados
de forma contígua (sem gaps > 500 ms entre bytes do mesmo frame).

---

## Verificação Rápida

Para confirmar que o ESP32 está recebendo corretamente, observar o log serial:

```
I (1234) uart_fft: Task iniciada (UART2 RX=GPIO16)
```

Quando frames chegam, o browser exibe o espectro e o status mostra:
```
sr=44100 Hz | 12:34:56
```
