#pragma once
#include "node_core.h"
#if defined(FB_ROLE_BATTERY)
#define APP_NODE_ID FB_NODE_BATTERY
#define APP_HAS_SENSE_TASK 1
#elif defined(FB_ROLE_BODY)
#define APP_NODE_ID FB_NODE_BODY
#define APP_HAS_SENSE_TASK 0
#else
#error "define FB_ROLE_BATTERY or FB_ROLE_BODY"
#endif
void app_hw_init(void);
void app_sensors_read(fb_sensors_t *s);
void app_actuators_apply(const fb_actuators_t *a);
