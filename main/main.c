#include "editor.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_log.h"
#include "esp_check.h"
void app_main(void) {
    bsp_display_cfg_t cfg={
        .lvgl_port_cfg=ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size=480*60,
        .double_buffer=true,
        /* esp_lvgl_port uses this flag for both rotation backends.
         * CONFIG_LVGL_PORT_ENABLE_PPA selects hardware; keep it enabled. */
        .flags={.buff_dma=false,.buff_spiram=true,.sw_rotate=true}
    };
    cfg.lvgl_port_cfg.task_stack=12288;
    lv_display_t *display=bsp_display_start_with_config(&cfg);
    if(!display){ESP_LOGE("prototype","Display/touch init failed");return;}
    if(!bsp_display_lock(5000)){ESP_LOGE("prototype","Display lock timed out");return;}
    /* Fixed landscape, flipped 180 degrees from the initial prototype. */
    lv_display_set_rotation(display,LV_DISPLAY_ROTATION_270);
    editor_create();
    ESP_LOGI("prototype", "render period=%d ms; PPA rotation=%d", CONFIG_LV_DEF_REFR_PERIOD, CONFIG_LVGL_PORT_ENABLE_PPA);
    bsp_display_unlock();
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    ESP_LOGI("prototype","full editor home started; 800x480 fixed rotation=270");
}
