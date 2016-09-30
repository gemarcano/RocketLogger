#include "types.h"
#include "gpio.h"
#include "pwm.h"
#include "rl_low_level.h"

#define FHR1_GPIO 30
#define FHR2_GPIO 60
#define LED_STATUS_GPIO 45
#define LED_ERROR_GPIO 44

void hw_init(struct rl_conf_new* conf);
void hw_close();

int hw_sample(struct rl_conf_new* confn);