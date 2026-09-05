#include "ui_claude_status.h"
#include "claude_assets.h"
#include "main.h"
#include "led_control.h"
#include <string.h>

static lv_obj_t *g_overlay;
static lv_obj_t *g_img;
static lv_obj_t *g_label;
static lv_timer_t *g_timer;
static const uint8_t *g_data;
static int g_frames;
static int g_frame;
static lv_image_dsc_t g_dsc;

static void frame_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_img || !g_data || g_frames <= 0) return;
    g_frame = (g_frame + 1) % g_frames;
    g_dsc.data = (uint8_t *)(g_data + g_frame * CLAUDE_FRAME_BYTES);
    lv_image_set_src(g_img, &g_dsc);
}

void ui_claude_status_init(lv_obj_t *parent)
{
    (void)parent;
    /* Build on the top layer so the status overlay is always above the home
     * screen and any later-created windows (z-order-independent). */
    g_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(g_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_overlay, 0, 0);
    lv_obj_set_style_pad_all(g_overlay, 0, 0);
    lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);
    g_img = lv_image_create(g_overlay);
    lv_obj_set_size(g_img, CLAUDE_FRAME_W, CLAUDE_FRAME_H);
    lv_obj_align(g_img, LV_ALIGN_CENTER, 0, -20);
    memset(&g_dsc, 0, sizeof(g_dsc));
    g_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    g_dsc.header.w = CLAUDE_FRAME_W;
    g_dsc.header.h = CLAUDE_FRAME_H;
    g_dsc.header.stride = CLAUDE_FRAME_W * 2;
    g_dsc.data_size = CLAUDE_FRAME_BYTES;
    g_label = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(g_label, g_misans_normal_16, 0);
    lv_obj_set_style_text_color(g_label, lv_color_white(), 0);
    lv_obj_align(g_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
    g_timer = lv_timer_create(frame_cb, 150, NULL);
    lv_timer_pause(g_timer);
}

void ui_claude_status_set(const char *state)
{
    const char *text = NULL;
    if (!state) state = "default";
    if (strcmp(state, "idle") == 0) { g_data = claude_idle_data; g_frames = CLAUDE_IDLE_FRAMES; text = "休息中"; }
    else if (strcmp(state, "thinking") == 0) { g_data = claude_thinking_data; g_frames = CLAUDE_THINKING_FRAMES; text = "正在思考中"; }
    else if (strcmp(state, "executing") == 0) { g_data = claude_executing_data; g_frames = CLAUDE_EXECUTING_FRAMES; text = "正在工作中"; }
    if (!text) {
        lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_timer_pause(g_timer);
        return;
    }
    g_frame = 0;
    g_dsc.data = (uint8_t *)g_data;
    lv_image_set_src(g_img, &g_dsc);
    lv_label_set_text(g_label, text);
    lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_overlay);
    lv_timer_resume(g_timer);
}

void ui_claude_status_deinit(void)
{
    if (g_timer) lv_timer_del(g_timer);
    if (g_overlay) lv_obj_del(g_overlay);
    g_timer = NULL; g_overlay = NULL; g_img = NULL; g_label = NULL;
}
