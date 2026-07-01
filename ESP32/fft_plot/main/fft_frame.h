#pragma once
#include <stdint.h>
#include "sdkconfig.h"

#define FFT_BINS     CONFIG_FFT_BINS
#define FRAME_SYNC_0 0xAA
#define FRAME_SYNC_1 0x55
#define FRAME_TOTAL  (2 + 4 + (FFT_BINS * 2))   // sync + sample_rate + data = 518

typedef struct __attribute__((packed)) {
    uint8_t  sync[2];
    uint32_t sample_rate;
    uint16_t data[FFT_BINS];
} fft_frame_t;

_Static_assert(sizeof(fft_frame_t) == FRAME_TOTAL, "fft_frame_t size mismatch");
