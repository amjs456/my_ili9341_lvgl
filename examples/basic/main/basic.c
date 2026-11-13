#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "my_ili9341_lvgl.h"

#include "esp_err.h"
#include "portmacro.h"
#include "sys/lock.h"
#include "display/lv_display.h"
#include <FreeRTOS/FreeRTOS.h>
#include <stdint.h>
#include <stdio.h>
#include "math.h"

static QueueHandle_t data_queue;


extern void example_lvgl_demo_ui(lv_display_t *disp);

void app_main(void)
{
    lv_display_t *display = NULL;

    ESP_ERROR_CHECK(my_ili9341_lvgl_init(&display));
    xTaskCreate(lvgl_port_task, "LVGL", 8 * 1024, NULL, 2, NULL);

    _lock_acquire(&lvgl_api_lock);
    example_lvgl_demo_ui(display);
    _lock_release(&lvgl_api_lock);
}