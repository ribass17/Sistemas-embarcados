#pragma once

// SIMULADOR STM32 — REMOVER QUANDO O HARDWARE ESTIVER PRONTO
// Usa UART_NUM_1 (GPIO 4 TX) em loopback com UART_NUM_2 (GPIO 16 RX).
// Conexão: fio entre GPIO 4 e GPIO 16.
void stm32_sim_start(void);
