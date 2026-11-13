idf_component_register(SRCS "my_ili9341_lvgl.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_timer
                    PRIV_REQUIRES esp_driver_spi esp_lcd esp_lcd_ili9341)
