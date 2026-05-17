#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include "brand_icons.h"
#include "secrets.h"

#define LCD_HOST SPI3_HOST
#define LCD_WIDTH 280
#define LCD_HEIGHT 240
#define LCD_OFFSET_X 20
#define LCD_OFFSET_Y 0
#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
#define LCD_DRAW_BUFFER_LINES 20
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
static lv_display_t *s_lv_display;

typedef struct {
    lv_obj_t *current_pct;
    lv_obj_t *weekly_pct;
    lv_obj_t *status;
    lv_obj_t *current_bar;
    lv_obj_t *weekly_bar;
} source_widgets_t;

static source_widgets_t s_claude_ui;
static source_widgets_t s_codex_ui;
static lv_obj_t *s_header_status;

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

static lv_color_t pct_color(int pct)
{
    if (pct < 0) return lv_color_hex(0x5a606e);
    if (pct < 50) return lv_color_hex(0x23be78);
    if (pct < 75) return lv_color_hex(0xf0b42d);
    if (pct < 90) return lv_color_hex(0xf56e37);
    return lv_color_hex(0xeb3c50);
}

static void set_label(lv_obj_t *obj, const char *fmt, ...)
{
    char text[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    lv_label_set_text(obj, text);
}

static void style_bar(lv_obj_t *bar, lv_color_t color)
{
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 176, 12);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x232935), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
}

static void style_label(lv_obj_t *label, lv_color_t color, const lv_font_t *font)
{
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
}

static void create_source_row(
    lv_obj_t *screen,
    int y,
    const char *name,
    const lv_image_dsc_t *icon,
    source_widgets_t *widgets)
{
    lv_obj_t *img = lv_image_create(screen);
    lv_image_set_src(img, icon);
    lv_obj_set_pos(img, 14, y + 3);

    lv_obj_t *name_label = lv_label_create(screen);
    lv_label_set_text(name_label, name);
    style_label(name_label, lv_color_hex(0xeef2f7), &lv_font_montserrat_18);
    lv_obj_set_pos(name_label, 56, y);

    widgets->current_pct = lv_label_create(screen);
    style_label(widgets->current_pct, lv_color_hex(0x55d2ff), &lv_font_montserrat_20);
    lv_obj_set_width(widgets->current_pct, 70);
    lv_obj_set_style_text_align(widgets->current_pct, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(widgets->current_pct, 184, y - 2);

    widgets->current_bar = lv_bar_create(screen);
    style_bar(widgets->current_bar, lv_color_hex(0x55d2ff));
    lv_obj_set_pos(widgets->current_bar, 56, y + 28);

    lv_obj_t *week_label = lv_label_create(screen);
    lv_label_set_text(week_label, "WEEK");
    style_label(week_label, lv_color_hex(0x909aaa), &lv_font_montserrat_12);
    lv_obj_set_pos(week_label, 56, y + 46);

    widgets->weekly_bar = lv_bar_create(screen);
    style_bar(widgets->weekly_bar, lv_color_hex(0x55d2ff));
    lv_obj_set_size(widgets->weekly_bar, 64, 8);
    lv_obj_set_pos(widgets->weekly_bar, 98, y + 50);

    widgets->weekly_pct = lv_label_create(screen);
    style_label(widgets->weekly_pct, lv_color_hex(0x909aaa), &lv_font_montserrat_12);
    lv_obj_set_pos(widgets->weekly_pct, 170, y + 45);

    widgets->status = lv_label_create(screen);
    style_label(widgets->status, lv_color_hex(0x909aaa), &lv_font_montserrat_12);
    lv_obj_set_width(widgets->status, 54);
    lv_obj_set_style_text_align(widgets->status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(widgets->status, 204, y + 45);
}

static void update_source_row(source_widgets_t *widgets, const source_status_t *source)
{
    int current = MAX(0, MIN(source->current_pct, 100));
    int weekly = MAX(0, MIN(source->weekly_pct, 100));
    lv_color_t current_color = strcmp(source->status, "ok") == 0 ? pct_color(source->current_pct) : lv_color_hex(0xeb505a);
    lv_color_t weekly_color = pct_color(source->weekly_pct);

    set_label(widgets->current_pct, "%d%%", source->current_pct);
    lv_obj_set_style_text_color(widgets->current_pct, current_color, 0);
    lv_obj_set_style_bg_color(widgets->current_bar, current_color, LV_PART_INDICATOR);
    lv_bar_set_value(widgets->current_bar, current, LV_ANIM_ON);

    set_label(widgets->weekly_pct, "%d%%", source->weekly_pct);
    lv_obj_set_style_bg_color(widgets->weekly_bar, weekly_color, LV_PART_INDICATOR);
    lv_bar_set_value(widgets->weekly_bar, weekly, LV_ANIM_ON);

    lv_label_set_text(widgets->status, strcmp(source->status, "ok") == 0 ? "OK" : source->status);
    lv_obj_set_style_text_color(widgets->status, strcmp(source->status, "ok") == 0 ? lv_color_hex(0x50dc96) : lv_color_hex(0xeb505a), 0);
}

static void ui_build_status_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0c0f16), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "VIBECODE");
    style_label(title, lv_color_hex(0x55d2ff), &lv_font_montserrat_20);
    lv_obj_set_pos(title, 14, 10);

    s_header_status = lv_label_create(screen);
    style_label(s_header_status, lv_color_hex(0x50dc96), &lv_font_montserrat_12);
    lv_obj_set_width(s_header_status, 94);
    lv_obj_set_style_text_align(s_header_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_header_status, 162, 16);

    create_source_row(screen, 54, "CLAUDE", &brand_icon_claude, &s_claude_ui);
    create_source_row(screen, 128, "CODEX", &brand_icon_openai, &s_codex_ui);

    lv_obj_t *refresh = lv_label_create(screen);
    lv_label_set_text(refresh, "BTN REFRESH");
    style_label(refresh, lv_color_hex(0x788291), &lv_font_montserrat_12);
    lv_obj_set_pos(refresh, 14, 216);

    lv_obj_t *spinner = lv_spinner_create(screen);
    lv_obj_set_size(spinner, 22, 22);
    lv_obj_set_pos(spinner, 232, 208);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x263040), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x55d2ff), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);
}

static void ui_show_message(const char *line1, const char *line2)
{
    if (!lvgl_port_lock(0)) {
        return;
    }
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0c0f16), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, line1);
    style_label(title, lv_color_hex(0xeef2f7), &lv_font_montserrat_20);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -18);

    lv_obj_t *message = lv_label_create(screen);
    lv_label_set_text(message, line2);
    style_label(message, lv_color_hex(0x909aaa), &lv_font_montserrat_14);
    lv_obj_set_width(message, 240);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_CENTER, 0, 16);
    lvgl_port_unlock();
}

static void ui_update_status(const usage_status_t *status)
{
    if (!lvgl_port_lock(0)) {
        return;
    }
    ui_build_status_screen();
    lv_label_set_text(s_header_status, status->ok ? "LIVE" : status->message);
    lv_obj_set_style_text_color(s_header_status, status->ok ? lv_color_hex(0x50dc96) : lv_color_hex(0xeb505a), 0);
    update_source_row(&s_claude_ui, &status->claude);
    update_source_row(&s_codex_ui, &status->codex);
    lvgl_port_unlock();
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
        .max_transfer_sz = LCD_WIDTH * LCD_DRAW_BUFFER_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
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
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, LCD_OFFSET_X, LCD_OFFSET_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    lv_init();
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 16;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = s_panel_io,
        .panel_handle = s_panel,
        .buffer_size = LCD_WIDTH * LCD_DRAW_BUFFER_LINES,
        .double_buffer = false,
        .hres = LCD_WIDTH,
        .vres = LCD_HEIGHT,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = 1,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };
    s_lv_display = lvgl_port_add_disp(&display_cfg);
    ESP_ERROR_CHECK(s_lv_display ? ESP_OK : ESP_FAIL);
    lv_display_set_default(s_lv_display);

    ui_show_message("BOOTING", "display ok");
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
    ui_show_message("WIFI", WIFI_SSID);

    if (!wifi_connect()) {
        ESP_LOGE(TAG, "WiFi failed");
        ui_show_message("WIFI FAIL", "check ssid/pass");
        return;
    }

    usage_status_t status;
    while (true) {
        ui_show_message("FETCHING", "vibecode api");
        bool got = fetch_usage(&status);
        if (!got) {
            status.ok = false;
        }
        ui_update_status(&status);

        for (int i = 0; i < 240; i++) {
            if (gpio_get_level(PIN_BUTTON) == 0) {
                vTaskDelay(pdMS_TO_TICKS(250));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}
