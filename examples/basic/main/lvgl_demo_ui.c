/*
 * XPT2046 touch orientation test UI for:
 *   amjs456/my_ili9341_lvgl/examples/basic/main/lvgl_demo_ui.c
 * LVGL 9, 320x240 landscape.
 *
 * Note: assumes the first LVGL POINTER input device was registered by
 * my_ili9341_lvgl.c with its user_data set to esp_lcd_touch_handle_t.
 */
#include "lvgl.h"
#include "esp_lcd_touch.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

static lv_indev_t *s_indev;
static esp_lcd_touch_handle_t s_touch;
static lv_obj_t *s_coord_label;
static lv_obj_t *s_marker;
static lv_obj_t *s_button_labels[3];
static lv_obj_t *s_buttons[3];
static bool s_swap_xy;
static bool s_mirror_x;
static bool s_mirror_y;
static const int s_button_ids[3] = {0, 1, 2};

static void update_button_labels(void)
{
    const bool values[3] = {s_swap_xy, s_mirror_x, s_mirror_y};
    const char *names[3] = {"swap_xy", "mirror_x", "mirror_y"};

    for (int i = 0; i < 3; i++) {
        lv_label_set_text_fmt(s_button_labels[i], "%s\n%d", names[i], values[i] ? 1 : 0);
        lv_obj_center(s_button_labels[i]);
        lv_obj_set_style_bg_color(s_buttons[i],
                                  values[i] ? lv_palette_main(LV_PALETTE_GREEN)
                                            : lv_palette_main(LV_PALETTE_GREY),
                                  LV_PART_MAIN);
    }
}

static void button_clicked(lv_event_t *e)
{
    if (!s_touch) return;

    int which = *(const int *)lv_event_get_user_data(e);
    esp_err_t err = ESP_OK;

    if (which == 0) {
        bool next = !s_swap_xy;
        err = esp_lcd_touch_set_swap_xy(s_touch, next);
        if (err == ESP_OK) s_swap_xy = next;
    } else if (which == 1) {
        bool next = !s_mirror_x;
        err = esp_lcd_touch_set_mirror_x(s_touch, next);
        if (err == ESP_OK) s_mirror_x = next;
    } else if (which == 2) {
        bool next = !s_mirror_y;
        err = esp_lcd_touch_set_mirror_y(s_touch, next);
        if (err == ESP_OK) s_mirror_y = next;
    }

    if (err == ESP_OK) {
        update_button_labels();
    } else {
        lv_label_set_text_fmt(s_coord_label, "Setting error: %s", esp_err_to_name(err));
    }
}

static void touch_position_timer(lv_timer_t *timer)
{
    (void)timer;
    if (!s_indev) return;

    lv_point_t p;
    lv_indev_get_point(s_indev, &p);
    bool pressed = (lv_indev_get_state(s_indev) == LV_INDEV_STATE_PRESSED);

    lv_label_set_text_fmt(s_coord_label, "X:%ld  Y:%ld  %s",
                          (long)p.x, (long)p.y, pressed ? "DOWN" : "UP");

    if (pressed) {
        lv_obj_set_pos(s_marker, p.x - 5, p.y - 5);
        lv_obj_remove_flag(s_marker, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_marker, LV_OBJ_FLAG_HIDDEN);
    }
}

static void create_corner(lv_obj_t *parent, const char *name, int x, int y)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_pos(dot, x - 6, y - 6);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    /* Keep the labels inside the display on both sides. */
    lv_obj_set_pos(label, x < 160 ? x + 8 : x - 26, y - 9);
}

void example_lvgl_demo_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* The existing component registers XPT2046 as a POINTER input device. */
    for (lv_indev_t *id = lv_indev_get_next(NULL); id; id = lv_indev_get_next(id)) {
        if (lv_indev_get_type(id) == LV_INDEV_TYPE_POINTER &&
            lv_indev_get_display(id) == disp) {
            s_indev = id;
            break;
        }
    }
    if (s_indev) {
        s_touch = (esp_lcd_touch_handle_t)lv_indev_get_user_data(s_indev);
    }

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "XPT2046 TOUCH TEST (320x240)");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_pos(title, 6, 4);

    s_coord_label = lv_label_create(scr);
    lv_label_set_text(s_coord_label, "X:---  Y:---  UP");
    lv_obj_set_style_text_color(s_coord_label, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_pos(s_coord_label, 6, 26);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Tap blue dots; red dot must follow.");
    lv_obj_set_style_text_color(hint, lv_color_black(), 0);
    lv_obj_set_pos(hint, 6, 46);

    create_corner(scr, "TL", 18, 80);
    create_corner(scr, "TR", 301, 80);
    create_corner(scr, "BL", 18, 170);
    create_corner(scr, "BR", 301, 170);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_button_create(scr);
        lv_obj_set_size(btn, 100, 43);
        lv_obj_set_pos(btn, 4 + i * 106, 194);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_add_event_cb(btn, button_clicked, LV_EVENT_CLICKED,
                            (void *)&s_button_ids[i]);
        s_buttons[i] = btn;
        s_button_labels[i] = lv_label_create(btn);
        lv_obj_set_style_text_align(s_button_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    }

    /* Use the driver's *actual* starting orientation. */
    if (s_touch) {
        esp_lcd_touch_get_swap_xy(s_touch, &s_swap_xy);
        esp_lcd_touch_get_mirror_x(s_touch, &s_mirror_x);
        esp_lcd_touch_get_mirror_y(s_touch, &s_mirror_y);
    } else {
        lv_label_set_text(s_coord_label, "ERROR: touch device not found");
    }
    update_button_labels();

    /* Create last so the red marker stays visible above other widgets. */
    s_marker = lv_obj_create(scr);
    lv_obj_set_size(s_marker, 10, 10);
    lv_obj_set_style_radius(s_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_marker, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(s_marker, 0, 0);
    lv_obj_set_style_pad_all(s_marker, 0, 0);
    lv_obj_remove_flag(s_marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_marker, LV_OBJ_FLAG_HIDDEN);

    if (s_indev && s_touch) lv_timer_create(touch_position_timer, 30, NULL);
}
