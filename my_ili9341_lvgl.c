#include "my_ili9341_lvgl.h"

#include "display/lv_display.h"
#include "draw/sw/lv_draw_sw_utils.h"
#include "esp_check.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal/spi_types.h"
#include "indev/lv_indev.h"
#include "misc/lv_area.h"
#include "misc/lv_color.h"
#include "misc/lv_timer.h"
#include "misc/lv_types.h"
#include "sdkconfig.h"
#include "esp_lcd_ili9341.h"
#include "lvgl.h"
#include "sys/lock.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/param.h>
#include <sys/unistd.h>

#define LCD_HOST SPI2_HOST
#define TOUCH_HOST SPI3_HOST

#define LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)
#define LCD_H_RES 320
#define LCD_V_RES 240
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8

#define LVGL_DRAW_BUF_LINES 20
#define LVGL_TICK_PERIOD_MS 1
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS (1000 / CONFIG_FREERTOS_HZ)

static const char *TAG = "my_ili9341_lvgl";

_lock_t lvgl_api_lock;

static spi_bus_config_t bus_config = {
    .sclk_io_num = CONFIG_ILI9341_SPI_SCK_GPIO,
    .mosi_io_num = CONFIG_ILI9341_SPI_MOSI_GPIO,
    .miso_io_num = CONFIG_ILI9341_SPI_MISO_GPIO,
    .quadhd_io_num = -1,
    .quadwp_io_num = -1,
    .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),
};


esp_lcd_panel_io_handle_t io_handle = NULL;

static esp_lcd_panel_io_spi_config_t io_config = {
    .dc_gpio_num = CONFIG_ILI9341_SPI_DC_GPIO,
    .cs_gpio_num = CONFIG_ILI9341_SPI_CS_GPIO,
    .pclk_hz = LCD_PIXEL_CLOCK_HZ,
    .lcd_cmd_bits = LCD_CMD_BITS,
    .lcd_param_bits = LCD_PARAM_BITS,
    .spi_mode = 0,
    .trans_queue_depth = 10,
};

esp_lcd_panel_handle_t panel_handle = NULL;

static esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = CONFIG_ILI9341_SPI_RST_GPIO,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
    .bits_per_pixel = 16,
};

static spi_bus_config_t touch_bus_config = {
    .sclk_io_num = CONFIG_ILI9341_TOUCH_CLK,
    .mosi_io_num = CONFIG_ILI9341_TOUCH_DIN,
    .miso_io_num = CONFIG_ILI9341_TOUCH_DO,
    .quadhd_io_num = -1,
    .quadwp_io_num = -1,
    .max_transfer_sz = 64,
};

static esp_lcd_panel_io_handle_t tp_io_handle = NULL;
static esp_lcd_touch_handle_t tp = NULL;

static esp_lcd_touch_config_t tp_cfg = {
    .x_max = LCD_H_RES,
    .y_max = LCD_V_RES,
    .rst_gpio_num = -1,
    .int_gpio_num = -1,
    .flags = {
        .swap_xy = 1,
        .mirror_x = 0,
        .mirror_y = 0,
    }
};

static esp_err_t init_spi_bus(void){
    esp_err_t err = spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t install_panel_io(void){
    esp_err_t err = esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &io_handle);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t install_lcd_panel(void){
    esp_err_t err = esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_lcd_new_panel_ili9341 failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_reset(panel_handle);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_lcd_panel_reset failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_init(panel_handle);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_lcd_panel_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_swap_xy(panel_handle, true);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_lcd_panel_swap_xy failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_mirror(panel_handle, true, true);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_lcd_panel_mirror failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_disp_on_off(panel_handle, true);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_lcd_panel_disp_on_off failed");
        return err;
    }
    return ESP_OK;
}

static esp_err_t init_lcd_panel(void){
    esp_err_t err = install_panel_io();
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "install_panel_io failed");
        return err;
    }

    err = install_lcd_panel();
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "install_lcd_panel failed");
        return err;
    }
    return ESP_OK;
}

void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map){
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    lv_draw_sw_rgb565_swap(px_map, (offsetx2 + 1 - offsetx1) * (offsety2 + 1 - offsety1));
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel_handle, offsetx1,  offsety1, offsetx2 + 1, offsety2 + 1, px_map);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
        lv_display_flush_ready(disp);
    }
}

static void increase_lvgl_tick(void *arg){
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx){
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static esp_err_t install_lvgl_tick_timer(lv_display_t *out_display) {
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    esp_err_t err = esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_timer_create failed");
        return err;
    }

    err = esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_timer_start_periodic failed");
        return err;
    }

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    err = esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, out_display);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_lcd_panel_io_register_event_callbacks failed");
        return err;
    }
    return ESP_OK;
}

static esp_err_t init_lvgl(lv_display_t **out_display){
    if(out_display == NULL){
        return ESP_ERR_INVALID_ARG;
    }
    lv_init();
    *out_display = lv_display_create(LCD_H_RES, LCD_V_RES);

    if(*out_display == NULL){
        return ESP_ERR_NO_MEM;
    }
    size_t draw_buffer_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);

    void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
    if(buf1 == NULL){
        return ESP_ERR_NO_MEM;
    }
    void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
    if(buf2 == NULL){
        return ESP_ERR_NO_MEM;
    }

    lv_display_set_buffers(*out_display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(*out_display, panel_handle);
    lv_display_set_color_format(*out_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(*out_display, my_flush_cb);

    esp_err_t err = install_lvgl_tick_timer(*out_display);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "install_lvgl_tick_timer failed");
        return err;
    }

    return ESP_OK;
}

esp_err_t lcd_touch_install(){
    esp_err_t err = spi_bus_initialize(TOUCH_HOST, &touch_bus_config, SPI_DMA_CH_AUTO);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_spi_config_t tp_io_config = 
        ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(
            CONFIG_ILI9341_TOUCH_CS
        );
    err = esp_lcd_new_panel_io_spi(TOUCH_HOST, &tp_io_config, &tp_io_handle);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &tp);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "esp_lcd_touch_new_spi_xpt2046: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}


static void my_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data){
    esp_lcd_touch_handle_t touch = lv_indev_get_user_data(indev);
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t count = 0;

    esp_err_t err = esp_lcd_touch_read_data(touch);

    if(err == ESP_OK && esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &count, 1) && count > 0){
        x = (x - 29) * 283 / 188 + 18;
        y = (y - 111) * 90 / 110 + 80;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}


static esp_err_t register_lvgl_touch(lv_display_t *display){
    lv_indev_t *indev = lv_indev_create();
    
    if(indev == NULL){
        return ESP_ERR_NO_MEM;
    }
    
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, display);
    lv_indev_set_user_data(indev, tp);
    lv_indev_set_read_cb(indev, my_touch_read_cb);
    return ESP_OK;
}

esp_err_t my_ili9341_lvgl_init(lv_display_t **out_display){
    if(out_display == NULL){
        return ESP_ERR_INVALID_ARG;
    }

    *out_display = NULL;
    
    esp_err_t err = init_spi_bus();
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "init_spi_bus failed");
        return err;
    }

    err = init_lcd_panel();
    if(err != ESP_OK){
        ESP_LOGE(TAG, "init_lcd_panel failed");
        return err;
    }

    err = lcd_touch_install();
    if(err != ESP_OK){
        ESP_LOGE(TAG, "lcd_touch_install failed: %s", esp_err_to_name(err));
        return err;
    }

    

    err = init_lvgl(out_display);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "init_lvgl failed");
        return err;
    }

    err = register_lvgl_touch(*out_display);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "register_lvgl_touch failed: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

void lvgl_port_task(void *arg){
    uint32_t time_till_next_ms = 0;
    while(1){
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}