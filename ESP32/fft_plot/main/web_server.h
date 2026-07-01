#pragma once
#include "esp_http_server.h"
#include "sdkconfig.h"
#include <stdint.h>
#include <stddef.h>

// Payload binário: 4 bytes sample_rate + FFT_BINS * 2 bytes magnitude
#define WS_PAYLOAD_MAX (4 + CONFIG_FFT_BINS * 2)

httpd_handle_t web_server_start(void);
void ws_broadcast_fft(const uint8_t *data, size_t len);
