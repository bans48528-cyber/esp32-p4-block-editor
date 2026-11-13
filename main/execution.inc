/* Fixed execution controls, matching the web client's compact viewport. */
static lv_timer_t *execution_timer;
static TransferSnapshot execution_previous;
static lv_obj_t *run_control, *bt_status_label;
static lv_obj_t *device_status_label, *device_status_dot;
static void refresh_device_status(void) {
  if (!device_status_label)
    return;
  BleScanSnapshot bt;
  ble_scan_snapshot(&bt);
  DeviceStatus value = device_status_view(&bt);
  const char *text = device_status_text(value);
  if (strcmp(lv_label_get_text(device_status_label), text)) {
    lv_label_set_text(device_status_label, text);
    lv_color_t color = lv_color_hex(device_status_color(value));
    lv_obj_set_style_text_color(device_status_label, color, 0);
    lv_obj_set_style_bg_color(device_status_dot, color, 0);
  }
}
static void refresh_connection_status(void) {
  if (!bt_status_label)
    return;
  BleScanSnapshot bt;
  ble_scan_snapshot(&bt);
  const char *text = "未连接";
  uint32_t color = 0x747474;
  if (bt.link == BLE_LINK_CONNECTING || bt.link == BLE_LINK_DISCOVERING) {
    text = "连接中";
    color = 0x0078cc;
  } else if (bt.link == BLE_LINK_READY) {
    text = "已连接";
    color = 0x188447;
  } else if (bt.link == BLE_LINK_ERROR) {
    text = "连接失败";
    color = 0xc44236;
  }
  if (strcmp(lv_label_get_text(bt_status_label), text)) {
    lv_label_set_text(bt_status_label, text);
    lv_obj_set_style_text_color(bt_status_label, lv_color_hex(color), 0);
  }
}
static void execution_dispose(void) {
  if (execution_timer) {
    lv_timer_delete(execution_timer);
    execution_timer = NULL;
  }
  run_control = NULL;
  bt_status_label = NULL;
  device_status_label = device_status_dot = NULL;
}
static void execution_poll(lv_timer_t *timer) {
  (void)timer;
  refresh_connection_status();
  refresh_device_status();
  TransferSnapshot current;
  ble_program_snapshot(&current);
  if (!memcmp(&current, &execution_previous, sizeof current))
    return;
  execution_previous = current;
  char text[128];
  switch (current.state) {
  case TRANSFER_PREPARING:
    say("正在确认设备已停止…");
    break;
  case TRANSFER_COMPILING:
    say("正在编译程序…");
    break;
  case TRANSFER_UPLOADING:
    snprintf(text, sizeof text, "正在下载程序：%u%%",
             current.total ? current.done * 100 / current.total : 0);
    say(text);
    break;
  case TRANSFER_SENT:
    say("程序已发送，已收到设备回应");
    break;
  case TRANSFER_STOPPING:
    say("正在停止…");
    break;
  case TRANSFER_STOPPED:
    if (!current.total)
      say("设备已停止，未发送暂停指令");
    else if (current.total == 1)
      say("已发送一次暂停请求，实际状态以设备上报为准");
    else
      say("已连续发送5次暂停请求，实际状态以设备上报为准");
    break;
  case TRANSFER_ERROR:
    if (current.error == -203)
      say("蓝牙传输长度不足，请重新连接设备");
    else if (current.error == -205)
      say("设备回应超时，请重新连接后重试");
    else if (current.error == -207)
      say("程序编译失败，请检查积木");
    else if (current.error == -208)
      say("等待设备停止超时，请使用软件或硬件暂停程序");
    else if (current.error == -209)
      say("设备未保持停止状态，未发送程序");
    else {
      snprintf(text, sizeof text, "程序发送失败（%d），请检查蓝牙连接",
               current.error);
      say(text);
    }
    break;
  default:
    break;
  }
  if (run_control)
    lv_obj_set_style_bg_color(
        run_control,
        lv_color_hex(current.state == TRANSFER_PREPARING ||
                             current.state == TRANSFER_COMPILING ||
                             current.state == TRANSFER_UPLOADING
                         ? 0xe5c100
                         : 0xffd600),
        0);
}
static void execution_click(lv_event_t *e) {
  bool run = (intptr_t)lv_event_get_user_data(e) != 0;
#ifndef ESP_PLATFORM
  say(run ? "电脑预览：下载并运行需在屏幕板连接蓝牙后使用"
          : "电脑预览：停止需在屏幕板连接蓝牙后使用");
#else
  BleScanSnapshot bt;
  ble_scan_snapshot(&bt);
  if (bt.link != BLE_LINK_READY) {
    say("请先通过蓝牙连接 Spark_AI");
    return;
  }
  if (run && !model_count(nodes, -1)) {
    say("请先放入积木");
    return;
  }
  if (run ? ble_program_run(nodes) : ble_program_stop())
    say(run ? "正在编译程序…" : "正在停止…");
  else
    say("操作未发送，请稍后重试");
#endif
}
static void play_icon(lv_event_t *e) {
  lv_area_t a;
  lv_obj_get_coords(lv_event_get_target_obj(e), &a);
  lv_draw_triangle_dsc_t d;
  lv_draw_triangle_dsc_init(&d);
  d.bg_color = lv_color_white();
  d.bg_opa = LV_OPA_COVER;
  d.p[0] = (lv_point_precise_t){a.x1 + 22, a.y1 + 14};
  d.p[1] = (lv_point_precise_t){a.x1 + 22, a.y1 + 44};
  d.p[2] = (lv_point_precise_t){a.x1 + 43, a.y1 + 29};
  lv_draw_triangle(lv_event_get_layer(e), &d);
}
static void execution_create(lv_obj_t *screen) {
  for (int i = 0; i < 2; i++) {
    lv_obj_t *b =
        box(screen, 662 + i * 66, 272, 58, 58, i ? 0xffd600 : 0xbcbcbc);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_color(b, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(b, 7, 0);
    lv_obj_set_style_shadow_ofs_y(b, 3, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(i ? 0xe5c100 : 0xaaaaaa),
                              LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, execution_click, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);
    if (i) {
      run_control = b;
      lv_obj_add_event_cb(b, play_icon, LV_EVENT_DRAW_MAIN, NULL);
    } else {
      lv_obj_t *icon = box(b, 18, 18, 23, 23, 0xffffff);
      lv_obj_set_style_radius(icon, 0, 0);
      lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    }
  }
  refresh_connection_status();
  refresh_device_status();
  ble_program_snapshot(&execution_previous);
  execution_timer = lv_timer_create(execution_poll, 200, NULL);
}
