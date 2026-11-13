/* UI polling keeps all LVGL calls on the display task. */
static lv_timer_t *bt_timer;
static lv_obj_t *bt_message, *bt_list;
static uint32_t bt_revision;
static BleScanDevice bt_peers[BLE_SCAN_CAPACITY];
static void bt_connect(lv_event_t *e) {
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  if (id >= 0 && id < BLE_SCAN_CAPACITY)
    ble_link_connect(&bt_peers[id]);
}
static void bt_disconnect(lv_event_t *e) {
  (void)e;
  ble_link_disconnect();
}
static void bt_update(lv_timer_t *timer) {
  (void)timer;
  BleScanSnapshot s;
  ble_scan_snapshot(&s);
  if (s.revision == bt_revision)
    return;
  bt_revision = s.revision;
  const char *message = "点击搜索，查找附近的 Spark_AI";
  if (s.state == BLE_SCAN_STARTING)
    message = "正在初始化蓝牙，请稍候";
  if (s.state == BLE_SCAN_RUNNING)
    message = "正在搜索附近设备…";
  if (s.state == BLE_SCAN_DONE)
    message =
        s.count ? "搜索完成，可再次搜索" : "未发现设备，请确认设备蓝牙已开启";
  if (s.state == BLE_SCAN_ERROR)
    message = "蓝牙初始化失败，请重启屏幕板后重试";
  if (s.state == BLE_SCAN_UNAVAILABLE)
    message = "电脑预览不扫描蓝牙，请在屏幕板上搜索";
  if (s.link == BLE_LINK_CONNECTING)
    message = "正在连接 Spark_AI…";
  if (s.link == BLE_LINK_DISCOVERING)
    message = "正在确认设备通讯服务…";
  if (s.link == BLE_LINK_READY)
    message = "Spark_AI 已连接，通讯就绪";
  if (s.link == BLE_LINK_ERROR)
    message = "连接失败，请靠近设备并确认未被占用，再点击连接";
  if (s.link == BLE_LINK_ERROR && s.link_error <= -100 && s.link_error >= -102)
    message = "设备通讯服务不匹配，请检查设备固件";
  lv_label_set_text(bt_message, message);
  lv_obj_clean(bt_list);
  int row = 0;
  for (int priority = 1; priority >= 0; priority--)
    for (int i = 0; i < s.count; i++) {
      BleScanDevice *d = &s.devices[i];
      bt_peers[i] = *d;
      if ((int)d->spark != priority)
        continue;
      lv_obj_t *card =
          box(bt_list, 0, row++ * 68, 746, 62, d->spark ? 0xd8efff : 0xffffff);
      lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t *name = label(card, d->name[0] ? d->name : "未命名设备");
      place_label(name, 12, 4);
      char detail[100];
      snprintf(detail, sizeof detail,
               "%02X:%02X:%02X:%02X:%02X:%02X    %d dBm%s", d->address[5],
               d->address[4], d->address[3], d->address[2], d->address[1],
               d->address[0], d->rssi, d->spark ? "    Spark_AI" : "");
      lv_obj_t *line = label(card, detail);
      place_label(line, 12, 32);
      lv_obj_set_style_text_font(line, &board_param_font, 0);
      if (d->spark) {
        bool active = s.link == BLE_LINK_READY ||
                      s.link == BLE_LINK_CONNECTING ||
                      s.link == BLE_LINK_DISCOVERING;
        bool same = d->address_type == s.peer.address_type &&
                    !memcmp(d->address, s.peer.address, 6);
        lv_obj_t *action =
            button(card, 620, 9, 112, 44, active && same ? "断开" : "连接",
                   active && same ? bt_disconnect : bt_connect, i);
        if (active && !same)
          lv_obj_add_state(action, LV_STATE_DISABLED);
      }
    }
}
static void bt_deleted(lv_event_t *e) {
  (void)e;
  if (bt_timer) {
    lv_timer_delete(bt_timer);
    bt_timer = NULL;
  }
  ble_scan_stop();
}
static void bt_search(lv_event_t *e) {
  (void)e;
  ble_scan_start();
  bt_update(NULL);
}
static void open_bluetooth(lv_event_t *e) {
  (void)e;
  close_modal(NULL);
  modal = box(lv_screen_active(), 0, 0, 800, 480, 0xf1f5fa);
  lv_obj_add_event_cb(modal, bt_deleted, LV_EVENT_DELETE, NULL);
  button(modal, 10, 8, 112, 44, "返回", close_modal, 0);
  lv_obj_t *title = label(modal, "蓝牙设备");
  place_label(title, 145, 16);
  button(modal, 646, 8, 142, 44, "搜索设备", bt_search, 0);
  bt_message = label(modal, "");
  place_label(bt_message, 20, 66);
  bt_list = box(modal, 20, 100, 760, 332, 0xf1f5fa);
  lv_obj_add_flag(bt_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(bt_list, LV_DIR_VER);
  lv_obj_t *hint = label(modal, "开启 Spark_AI 蓝牙，保持设备在附近");
  place_label(hint, 20, 444);
  bt_revision = UINT32_MAX;
  bt_update(NULL);
  bt_timer = lv_timer_create(bt_update, 300, NULL);
}
