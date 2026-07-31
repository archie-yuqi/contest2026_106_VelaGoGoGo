/****************************************************************************
 * app/home_scense/ui_status_bar.c
 * Shared time, Wi-Fi, and settings controls.
 ****************************************************************************/

#include "ui_status_bar.h"
#include "main.h"
#include "wifi_status.h"

#define STATUS_BAR_MAX_INSTANCES 8

typedef struct status_bar_instance_s {
    lv_obj_t *time;
    lv_obj_t *wifi_icon;
    status_bar_settings_cb_t settings_cb;
} status_bar_instance_t;

static status_bar_instance_t g_bars[STATUS_BAR_MAX_INSTANCES];
static unsigned int g_bar_count;

static void settings_click_cb(lv_event_t *event)
{
    status_bar_instance_t *bar = lv_event_get_user_data(event);
    if (bar && bar->settings_cb) bar->settings_cb();
}

static void update_wifi_icon(status_bar_instance_t *instance,
                             const wifi_status_t *status)
{
    /* 参考 Linux 项目:单个 WiFi 扇形符号,颜色随信号强弱分档变化
     * (弱→强:红→橙→黄→浅绿→绿),未连接时灰色。 */
    const uint32_t level_colors[5] = {
        0xE74C3C,  /* 0:极弱/几乎无,红 */
        0xE67E22,  /* 1:弱,橙 */
        0xF1C40F,  /* 2:中,黄 */
        0x82C91E,  /* 3:较强,浅绿 */
        0x58D68D,  /* 4:强,绿 */
    };
    const lv_color_t offline = lv_color_hex(0x566573);
    uint8_t lvl;

    lv_label_set_text(instance->wifi_icon, LV_SYMBOL_WIFI);

    if (!status->associated || !status->has_ip) {
        /* 未连接:灰色 WiFi 符号 */
        lv_obj_set_style_text_color(instance->wifi_icon, offline, 0);
        return;
    }

    /* 已连接:按信号等级(1~4)取色;无 RSSI 读数按最高档绿色 */
    lvl = status->has_rssi ? status->signal_level : 4;
    if (lvl > 4) lvl = 4;
    lv_obj_set_style_text_color(instance->wifi_icon,
                                lv_color_hex(level_colors[lvl]), 0);
}

lv_obj_t *ui_status_bar_create(lv_obj_t *parent,
                               status_bar_settings_cb_t settings_cb)
{
    status_bar_instance_t *instance;
    lv_obj_t *bar;
    lv_obj_t *settings;
    lv_obj_t *label;

    if (g_bar_count >= STATUS_BAR_MAX_INSTANCES) return NULL;
    instance = &g_bars[g_bar_count++];
    instance->settings_cb = settings_cb;

    bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCREEN_WIDTH, STATUS_BAR_HEIGHT);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0E1621), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    instance->time = lv_label_create(bar);
    lv_obj_align(instance->time, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_text_font(instance->time, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(instance->time, lv_color_hex(0xF7F9F9), 0);
    lv_label_set_text(instance->time, "--:--");

    settings = lv_button_create(bar);
    lv_obj_set_size(settings, 48, STATUS_BAR_HEIGHT);
    lv_obj_align(settings, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(settings, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings, 0, 0);
    lv_obj_add_event_cb(settings, settings_click_cb, LV_EVENT_CLICKED, instance);
    label = lv_label_create(settings);
    lv_label_set_text(label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF7F9F9), 0);
    lv_obj_center(label);

    /* 单个 WiFi 扇形符号(参考 Linux),颜色随信号强弱变化 */
    instance->wifi_icon = lv_label_create(bar);
    lv_obj_align_to(instance->wifi_icon, settings, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    lv_obj_set_style_text_font(instance->wifi_icon, &lv_font_montserrat_16, 0);
    lv_label_set_text(instance->wifi_icon, LV_SYMBOL_WIFI);
    update_wifi_icon(instance, &(wifi_status_t){0});
    return bar;
}

void ui_status_bar_refresh(lv_timer_t *timer)
{
    char time_text[8];
    wifi_status_t wifi;
    unsigned int i;

    (void)timer;
    wifi_status_get(&wifi);
    lv_snprintf(time_text, sizeof(time_text), "%02d:%02d",
                lv_subject_get_int(&hour_subject),
                lv_subject_get_int(&minute_subject));
    for (i = 0; i < g_bar_count; i++) {
        lv_label_set_text(g_bars[i].time, time_text);
        update_wifi_icon(&g_bars[i], &wifi);
    }
}
