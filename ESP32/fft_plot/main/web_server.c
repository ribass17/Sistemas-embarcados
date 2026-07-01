#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "web_server.h"

static const char *TAG = "web_server";

#define MAX_WS_CLIENTS 7

extern const uint8_t root_html_start[] asm("_binary_root_html_start");
extern const uint8_t root_html_end[]   asm("_binary_root_html_end");
extern const uint8_t main_js_start[]   asm("_binary_main_js_start");
extern const uint8_t main_js_end[]     asm("_binary_main_js_end");

static httpd_handle_t s_server = NULL;

// --- HTTP handlers ---

typedef struct {
    const char    *mime;
    const uint8_t *start;
    const uint8_t *end;
} static_file_t;

static esp_err_t static_handler(httpd_req_t *req)
{
    const static_file_t *f = req->user_ctx;
    httpd_resp_set_type(req, f->mime);
    httpd_resp_send(req, (const char *)f->start, f->end - f->start);
    return ESP_OK;
}

static const static_file_t s_html = { "text/html",       root_html_start, root_html_end };
static const static_file_t s_js   = { "text/javascript", main_js_start,   main_js_end   };

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
        ESP_LOGI(TAG, "WS conectado fd=%d", httpd_req_to_sockfd(req));
    return ESP_OK;
}

// --- Pool estático de slots (sem malloc) ---

typedef struct {
    bool    busy;
    int     fd;
    size_t  len;
    uint8_t payload[WS_PAYLOAD_MAX];
} ws_slot_t;

static ws_slot_t    s_slots[MAX_WS_CLIENTS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void ws_send_cb(void *arg)
{
    ws_slot_t *slot = (ws_slot_t *)arg;
    httpd_ws_frame_t pkt = {
        .type    = HTTPD_WS_TYPE_BINARY,
        .payload = slot->payload,
        .len     = slot->len,
    };
    if (httpd_ws_send_frame_async(s_server, slot->fd, &pkt) != ESP_OK)
        ESP_LOGW(TAG, "ws send fd=%d falhou", slot->fd);
    slot->busy = false;
}

void ws_broadcast_fft(const uint8_t *data, size_t len)
{
    if (!s_server || len > WS_PAYLOAD_MAX) return;

    size_t n = MAX_WS_CLIENTS;
    int fds[MAX_WS_CLIENTS];
    if (httpd_get_client_list(s_server, &n, fds) != ESP_OK) return;

    for (size_t i = 0; i < n; i++) {
        if (httpd_ws_get_fd_info(s_server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET)
            continue;

        ws_slot_t *slot = NULL;
        taskENTER_CRITICAL(&s_mux);
        for (int j = 0; j < MAX_WS_CLIENTS; j++) {
            if (!s_slots[j].busy) {
                s_slots[j].busy = true;
                slot = &s_slots[j];
                break;
            }
        }
        taskEXIT_CRITICAL(&s_mux);

        if (!slot) { ESP_LOGW(TAG, "sem slot livre"); continue; }

        memcpy(slot->payload, data, len);
        slot->fd  = fds[i];
        slot->len = len;

        if (httpd_queue_work(s_server, ws_send_cb, slot) != ESP_OK)
            slot->busy = false;
    }
}

// --- Inicialização ---

static const httpd_uri_t uri_root = {
    .uri="/", .method=HTTP_GET, .handler=static_handler, .user_ctx=(void*)&s_html };
static const httpd_uri_t uri_js = {
    .uri="/main.js", .method=HTTP_GET, .handler=static_handler, .user_ctx=(void*)&s_js };
static const httpd_uri_t uri_ws = {
    .uri="/ws", .method=HTTP_GET, .handler=ws_handler, .is_websocket=true };

httpd_handle_t web_server_start(void)
{
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = MAX_WS_CLIENTS;
    config.stack_size       = 8192;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar httpd");
        return NULL;
    }
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_js);
    httpd_register_uri_handler(s_server, &uri_ws);
    ESP_LOGI(TAG, "HTTP+WS iniciado");
    return s_server;
}
