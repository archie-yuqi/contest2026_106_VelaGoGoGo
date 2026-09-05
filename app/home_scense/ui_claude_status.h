#ifndef HOME_SCENSE_UI_CLAUDE_STATUS_H
#define HOME_SCENSE_UI_CLAUDE_STATUS_H
#include <lvgl/lvgl.h>
void ui_claude_status_init(lv_obj_t *parent);
void ui_claude_status_set(const char *state);
void ui_claude_status_deinit(void);
#endif
