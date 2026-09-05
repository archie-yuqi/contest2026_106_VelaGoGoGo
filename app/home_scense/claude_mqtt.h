#ifndef HOME_SCENSE_CLAUDE_MQTT_H
#define HOME_SCENSE_CLAUDE_MQTT_H

#include <lvgl/lvgl.h>

void claude_mqtt_init(void);
void claude_mqtt_poll(lv_timer_t *timer);
void claude_mqtt_deinit(void);

#endif
