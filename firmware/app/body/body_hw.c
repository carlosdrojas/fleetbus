/* Body node hardware: head/turn lights on PB0/PB1, fault LED = LD2, servo on TIM3_CH1 (PB4, powered from its own 5 V). */
#include "bsp.h"
#include "app_roles.h"

void app_hw_init(void) { bsp_servo_init(); }
void app_sensors_read(fb_sensors_t *s) { (void)s; }
void app_actuators_apply(const fb_actuators_t *a) { bsp_lights_set(a->lights); bsp_servo_set_deg(a->servo_deg); }
