#pragma once

#include <sys/lock.h>
#include "display/lv_display.h"
#include "esp_err.h"

extern _lock_t lvgl_api_lock;

esp_err_t my_ili9341_lvgl_init(lv_display_t **out_display);
void lvgl_port_task(void *arg);