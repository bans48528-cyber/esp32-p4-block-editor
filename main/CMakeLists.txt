idf_component_register(SRCS "main.c" "perf_probe.c" "ble_scan.c" "program.c" "spark_protocol.c" "device_status.c" "editor.c" "projects.c" "work_store.c" "model.c" "block_shape.c" "block_icons.c" "toolbar_icons.c" "thumbnails.c" "board_font.c" "board_param_font.c" INCLUDE_DIRS "." LDFRAGMENTS "display_iram.lf" REQUIRES pika_compiler espressif__esp32_p4_function_ev_board lvgl json spiffs nvs_flash esp_partition bt espressif__esp_hosted)

target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=lvgl_port_ppa_rotate" "-Wl,--wrap=esp_lcd_panel_draw_bitmap")

# This auxiliary library is not automatically included in IDF's linker
# fragment inputs. Register before IDF generates the final linker script.
if(TARGET lvgl_port_lib)
    __ldgen_add_component(lvgl_port_lib)
endif()
