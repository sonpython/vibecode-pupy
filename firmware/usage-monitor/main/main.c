#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "secrets.h"

#define LCD_HOST SPI3_HOST
#define LCD_WIDTH 280
#define LCD_HEIGHT 240
#define LCD_OFFSET_X 20
#define LCD_OFFSET_Y 0
#define PIN_LCD_SCLK GPIO_NUM_9
#define PIN_LCD_MOSI GPIO_NUM_10
#define PIN_LCD_CS GPIO_NUM_14
#define PIN_LCD_DC GPIO_NUM_8
#define PIN_LCD_RST GPIO_NUM_18
#define PIN_BACKLIGHT GPIO_NUM_13
#define HAS_BACKLIGHT 1
#define PIN_POWER_EN GPIO_NUM_NC
#define HAS_POWER_EN 0
#define PIN_BUTTON GPIO_NUM_5

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define MAX_HTTP_OUTPUT 2048

static const char *TAG = "vibecode";
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static uint16_t *s_framebuffer;
static uint16_t *s_flush_buffer;

typedef struct {
    int current_pct;
    int weekly_pct;
    int stale_sec;
    char status[20];
} source_status_t;

typedef struct {
    source_status_t claude;
    source_status_t codex;
    bool ok;
    char message[64];
} usage_status_t;

typedef struct {
    char *buf;
    int len;
    int cap;
} http_buffer_t;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void lcd_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    x = MAX(0, MIN(x, LCD_WIDTH - 1));
    y = MAX(0, MIN(y, LCD_HEIGHT - 1));
    w = MIN(w, LCD_WIDTH - x);
    h = MIN(h, LCD_HEIGHT - y);

    if (s_framebuffer) {
        for (int row = 0; row < h; row++) {
            uint16_t *line = s_framebuffer + (y + row) * LCD_WIDTH + x;
            for (int col = 0; col < w; col++) {
                line[col] = color;
            }
        }
        return;
    }

    if (!s_panel) {
        return;
    }
    uint16_t *line = heap_caps_malloc(w * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line) {
        return;
    }
    for (int i = 0; i < w; i++) {
        line[i] = color;
    }
    for (int row = 0; row < h; row++) {
        esp_lcd_panel_draw_bitmap(s_panel, x, y + row, x + w, y + row + 1, line);
    }
    free(line);
}

static void lcd_flush(void)
{
    if (!s_panel || !s_framebuffer) {
        return;
    }
    if (!s_flush_buffer) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, s_framebuffer);
        return;
    }

    const int chunk_h = 20;
    for (int y = 0; y < LCD_HEIGHT; y += chunk_h) {
        int h = MIN(chunk_h, LCD_HEIGHT - y);
        for (int row = 0; row < h; row++) {
            memcpy(s_flush_buffer + row * LCD_WIDTH, s_framebuffer + (y + row) * LCD_WIDTH, LCD_WIDTH * sizeof(uint16_t));
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_WIDTH, y + h, s_flush_buffer);
    }
}

static const uint8_t *glyph5x7(char c)
{
    static const uint8_t space[5] = {0, 0, 0, 0, 0};
    static const uint8_t dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t colon[5] = {0, 0x14, 0, 0x14, 0};
    static const uint8_t pct[5] = {0x62, 0x64, 0x08, 0x13, 0x23};
    static const uint8_t digits[10][5] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E},
        {0x00, 0x42, 0x7F, 0x40, 0x00},
        {0x42, 0x61, 0x51, 0x49, 0x46},
        {0x21, 0x41, 0x45, 0x4B, 0x31},
        {0x18, 0x14, 0x12, 0x7F, 0x10},
        {0x27, 0x45, 0x45, 0x45, 0x39},
        {0x3C, 0x4A, 0x49, 0x49, 0x30},
        {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36},
        {0x06, 0x49, 0x49, 0x29, 0x1E},
    };
    static const uint8_t letters[26][5] = {
        {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
        {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
        {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
        {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
        {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01},
        {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
        {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
        {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
        {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
        {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
        {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
        {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
        {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
    };
    if (c >= 'a' && c <= 'z') {
        c -= 32;
    }
    if (c >= 'A' && c <= 'Z') {
        return letters[c - 'A'];
    }
    if (c >= '0' && c <= '9') {
        return digits[c - '0'];
    }
    if (c == '-') return dash;
    if (c == ':') return colon;
    if (c == '%') return pct;
    return space;
}

static void draw_char(int x, int y, char c, uint16_t color, int scale)
{
    const uint8_t *g = glyph5x7(c);
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (g[col] & (1 << row)) {
                lcd_draw_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, uint16_t color, int scale)
{
    int cursor = x;
    while (*text) {
        draw_char(cursor, y, *text++, color, scale);
        cursor += 6 * scale;
    }
}

static const uint32_t ICON_CLAUDE[32] = {
    0x00e00000u, 0x01f07000u, 0x01f07800u, 0x00f87020u,
    0x00f870f0u, 0x007871f0u, 0x1c7c73f0u, 0x3e3c73e0u,
    0x1f1ee7e0u, 0x1fdeefc0u, 0x07ffff80u, 0x03ffff00u,
    0x00ffff02u, 0x007ffe7fu, 0x001fffffu, 0xfffffffcu,
    0xffffffc0u, 0x07fffffcu, 0x001fffffu, 0x007fffffu,
    0x01fffe1fu, 0x03ffff00u, 0x0fcfffc0u, 0x1f1ddfe0u,
    0x1e39def0u, 0x0079cf78u, 0x00f1c798u, 0x00e38780u,
    0x01c38380u, 0x01838180u, 0x00038000u, 0x00038000u,
};

static const uint32_t ICON_OPENAI[32] = {
    0x001fc000u, 0x003ff000u, 0x00fffb00u, 0x00f07fc0u,
    0x01c0fff0u, 0x03c1f8f8u, 0x0f87e03cu, 0x1f9f801cu,
    0x3f9e1e0eu, 0x7b987f8eu, 0x7398f7eeu, 0xe39be1feu,
    0xe39ff0feu, 0xe39e3c3eu, 0xe3981f0eu, 0xe3981fc7u,
    0xf3f81dc7u, 0x70f81ce3u, 0x787c1ce3u, 0x3e1f7ce3u,
    0x3f87fce7u, 0x3fe7dce7u, 0x39ff1cefu, 0x38fc3cfeu,
    0x38387cfcu, 0x1c01f9f8u, 0x1e07e1e0u, 0x0fff83c0u,
    0x07fe0780u, 0x01ff1f80u, 0x000fff00u, 0x0003fc00u,
};

static void draw_icon_mask(int x, int y, const uint32_t *mask, uint16_t color)
{
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            if (mask[row] & (1u << (31 - col))) {
                lcd_draw_rect(x + col, y + row, 1, 1, color);
            }
        }
    }
}

static uint16_t pct_color(int pct)
{
    if (pct < 0) return rgb565(90, 96, 110);
    if (pct < 50) return rgb565(35, 190, 120);
    if (pct < 75) return rgb565(240, 180, 45);
    if (pct < 90) return rgb565(245, 110, 55);
    return rgb565(235, 60, 80);
}

static void draw_bar(int x, int y, int w, int h, int pct, uint16_t color)
{
    int clamped = MAX(0, MIN(pct, 100));
    lcd_draw_rect(x, y, w, h, rgb565(34, 39, 50));
    lcd_draw_rect(x + 2, y + 2, (w - 4) * clamped / 100, h - 4, color);
}

static void draw_icon_claude(int cx, int cy, int frame)
{
    uint16_t bg = rgb565(12, 15, 22);
    (void)frame;
    lcd_draw_rect(cx - 17, cy - 17, 34, 34, bg);
    draw_icon_mask(cx - 16, cy - 16, ICON_CLAUDE, rgb565(217, 119, 87));
}

static void draw_icon_codex(int cx, int cy, int frame)
{
    uint16_t bg = rgb565(12, 15, 22);
    (void)frame;
    lcd_draw_rect(cx - 17, cy - 17, 34, 34, bg);
    draw_icon_mask(cx - 16, cy - 16, ICON_OPENAI, rgb565(85, 210, 255));
}

static void draw_activity(int frame)
{
    int x = 146 + (frame % 18) * 5;
    uint16_t bg = rgb565(12, 15, 22);
    uint16_t muted = rgb565(120, 130, 145);
    uint16_t active = rgb565(85, 210, 255);

    lcd_draw_rect(142, 214, 102, 14, bg);
    for (int i = 0; i < 18; i++) {
        lcd_draw_rect(146 + i * 5, 220, 2, 2, muted);
    }
    lcd_draw_rect(x, 218, 8, 6, active);
}

static void draw_source(int y, const char *name, const source_status_t *source, bool is_codex, int frame)
{
    char label[32];
    uint16_t white = rgb565(238, 242, 247);
    uint16_t muted = rgb565(145, 154, 170);
    uint16_t color = strcmp(source->status, "ok") == 0 ? pct_color(source->current_pct) : rgb565(235, 80, 90);

    if (is_codex) {
        draw_icon_codex(27, y + 14, frame);
    } else {
        draw_icon_claude(27, y + 14, frame);
    }
    draw_text(50, y, name, white, 2);
    snprintf(label, sizeof(label), "%d%%", source->current_pct);
    draw_text(178, y, label, color, 2);
    draw_bar(50, y + 22, 176, 14, source->current_pct, color);

    snprintf(label, sizeof(label), "WEEK %d%%", source->weekly_pct);
    draw_text(50, y + 42, label, muted, 1);
    draw_bar(128, y + 42, 58, 8, source->weekly_pct, pct_color(source->weekly_pct));

    if (strcmp(source->status, "ok") != 0) {
        draw_text(190, y + 42, source->status, rgb565(235, 80, 90), 1);
    }
}

static void draw_status_screen(const usage_status_t *status, int frame)
{
    lcd_draw_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, rgb565(12, 15, 22));
    draw_text(14, 12, "VIBECODE", rgb565(85, 210, 255), 2);
    draw_text(14, 34, status->ok ? "LIVE" : status->message, status->ok ? rgb565(80, 220, 150) : rgb565(240, 90, 90), 1);
    draw_source(58, "CLAUDE", &status->claude, false, frame);
    draw_source(132, "CODEX", &status->codex, true, frame);
    draw_text(14, 216, "BTN REFRESH", rgb565(120, 130, 145), 1);
    draw_activity(frame);
    lcd_flush();
}

static void draw_message(const char *line1, const char *line2)
{
    lcd_draw_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, rgb565(12, 15, 22));
    draw_text(20, 82, line1, rgb565(238, 242, 247), 2);
    draw_text(20, 112, line2, rgb565(145, 154, 170), 1);
    lcd_flush();
}

static void set_backlight(int level)
{
#if HAS_BACKLIGHT
    gpio_set_level(PIN_BACKLIGHT, level);
#else
    (void)level;
#endif
}

#if HAS_POWER_EN
static void set_display_power(int level)
{
    gpio_set_level(PIN_POWER_EN, level);
}
#endif

static void init_display(void)
{
#if HAS_POWER_EN
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = 1ULL << PIN_POWER_EN,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&pwr_cfg));
    set_display_power(1);
    vTaskDelay(pdMS_TO_TICKS(30));
#endif

#if HAS_BACKLIGHT
    gpio_config_t bk_cfg = {
        .pin_bit_mask = 1ULL << PIN_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_cfg));
    set_backlight(0);
#endif

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 60 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_panel_io));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_panel_io, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, LCD_OFFSET_X, LCD_OFFSET_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    s_framebuffer = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_flush_buffer = heap_caps_malloc(LCD_WIDTH * 20 * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!s_framebuffer || !s_flush_buffer) {
        ESP_LOGW(TAG, "framebuffer allocation failed, falling back to direct draws");
        free(s_framebuffer);
        free(s_flush_buffer);
        s_framebuffer = NULL;
        s_flush_buffer = NULL;
    }
    draw_message("BOOTING", "display ok");
    set_backlight(1);
    ESP_LOGI(TAG, "display initialized");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 8) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "retry wifi connection %d", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_connect(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buffer_t *out = (http_buffer_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0 && out && out->buf) {
        int copy = MIN(evt->data_len, out->cap - out->len - 1);
        if (copy > 0) {
            memcpy(out->buf + out->len, evt->data, copy);
            out->len += copy;
            out->buf[out->len] = 0;
        }
    }
    return ESP_OK;
}

static bool parse_source(cJSON *root, const char *name, source_status_t *out)
{
    cJSON *src = cJSON_GetObjectItem(root, name);
    if (!cJSON_IsObject(src)) {
        strlcpy(out->status, "missing", sizeof(out->status));
        out->current_pct = -1;
        out->weekly_pct = -1;
        return false;
    }
    cJSON *current = cJSON_GetObjectItem(src, "current_pct");
    cJSON *weekly = cJSON_GetObjectItem(src, "weekly_pct");
    cJSON *stale = cJSON_GetObjectItem(src, "stale_sec");
    cJSON *status = cJSON_GetObjectItem(src, "status");
    out->current_pct = cJSON_IsNumber(current) ? current->valueint : -1;
    out->weekly_pct = cJSON_IsNumber(weekly) ? weekly->valueint : -1;
    out->stale_sec = cJSON_IsNumber(stale) ? stale->valueint : -1;
    strlcpy(out->status, cJSON_IsString(status) ? status->valuestring : "error", sizeof(out->status));
    return strcmp(out->status, "ok") == 0;
}

static bool fetch_usage(usage_status_t *status)
{
    memset(status, 0, sizeof(*status));
    strlcpy(status->message, "FETCH ERR", sizeof(status->message));

    char response[MAX_HTTP_OUTPUT] = {0};
    http_buffer_t output = {
        .buf = response,
        .len = 0,
        .cap = sizeof(response),
    };
    esp_http_client_config_t config = {
        .url = USAGE_API_URL,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &output,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return false;
    }
    esp_http_client_set_header(client, "X-Device-Secret", DEVICE_SECRET);
    esp_http_client_set_header(client, "User-Agent", "vibecode-esp32/1.0");

    esp_err_t err = esp_http_client_perform(client);
    int http_status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP GET status=%d err=%s len=%d", http_status, esp_err_to_name(err), output.len);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || http_status != 200 || output.len <= 0) {
        snprintf(status->message, sizeof(status->message), "HTTP %d", http_status);
        return false;
    }

    cJSON *root = cJSON_Parse(response);
    if (!root) {
        strlcpy(status->message, "JSON ERR", sizeof(status->message));
        return false;
    }
    bool claude_ok = parse_source(root, "claude", &status->claude);
    bool codex_ok = parse_source(root, "codex", &status->codex);
    cJSON_Delete(root);
    status->ok = claude_ok && codex_ok;
    if (!status->ok) {
        strlcpy(status->message, "DEGRADED", sizeof(status->message));
    }
    return true;
}

static void configure_button(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    init_display();
    configure_button();
    draw_message("WIFI", WIFI_SSID);

    if (!wifi_connect()) {
        ESP_LOGE(TAG, "WiFi failed");
        draw_message("WIFI FAIL", "check ssid/pass");
        return;
    }

    usage_status_t status;
    while (true) {
        draw_message("FETCHING", "vibecode api");
        bool got = fetch_usage(&status);
        if (!got) {
            status.ok = false;
        }
        int frame = 0;
        draw_status_screen(&status, frame);

        for (int i = 0; i < 240; i++) {
            if (gpio_get_level(PIN_BUTTON) == 0) {
                vTaskDelay(pdMS_TO_TICKS(250));
                break;
            }
            frame++;
            draw_icon_claude(27, 72, frame);
            draw_icon_codex(27, 146, frame);
            draw_activity(frame);
            lcd_flush();
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}
