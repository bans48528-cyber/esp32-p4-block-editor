static bool thumbnail_saved;
static lv_timer_t *home_thumbnail_timer;
static lv_obj_t *home_thumbnail_cards[PROJECTS];
static int home_thumbnail_slot;
static void home_thumbnail_dispose(void) {
  if (home_thumbnail_timer) {
    lv_timer_delete(home_thumbnail_timer);
    home_thumbnail_timer = NULL;
  }
  memset(home_thumbnail_cards, 0, sizeof home_thumbnail_cards);
  home_thumbnail_slot = 0;
}
static void home_thumbnail_load_next(lv_timer_t *timer) {
  (void)timer;
  while (home_thumbnail_slot < PROJECTS &&
         !home_thumbnail_cards[home_thumbnail_slot])
    home_thumbnail_slot++;
  if (home_thumbnail_slot >= PROJECTS) {
    home_thumbnail_dispose();
    return;
  }
  int slot = home_thumbnail_slot++;
  show_thumbnail(home_thumbnail_cards[slot], slot);
  if (home_thumbnail_slot >= PROJECTS)
    home_thumbnail_dispose();
}
static void home_later(void *unused) {
  (void)unused;
  home();
}
static void editor_later(void *unused) {
  (void)unused;
  open_editor();
}
static bool save_active(void) {
  if (!storage_ready || active_project < 0)
    return false;
  Project old = library.items[active_project];
  memcpy(library.items[active_project].nodes, nodes, sizeof nodes);
  if (works_save(work_ids[active_project], &library.items[active_project])) {
    thumbnail_saved = update_thumbnail(active_project);
    return true;
  }
  library.items[active_project] = old;
  return false;
}
static void save_work(lv_event_t *e) {
  (void)e;
  say(save_active()
          ? (thumbnail_saved ? "作品已保存" : "作品已保存，预览更新失败")
          : "保存失败，请重试");
}
static void back_home(lv_event_t *e) {
  (void)e;
  if (!save_active()) {
    say("保存失败，已保留编辑内容");
    return;
  }
  lv_async_call(home_later, NULL);
}
static void enter_project(lv_event_t *e) {
  active_project = (int)(intptr_t)lv_event_get_user_data(e);
  memcpy(nodes, library.items[active_project].nodes, sizeof nodes);
  lv_async_call(editor_later, NULL);
}
static void create_project(lv_event_t *e) {
  if (!storage_ready) {
    say("存储不可用，原数据未修改");
    return;
  }
  if (work_catalog.count >= WORK_LIMIT) {
    say("最多6个作品，请先删除一个");
    return;
  }
  int slot = 0;
  int example = (int)(intptr_t)lv_event_get_user_data(e);
  Project created;
  Project *p = &created;
  memset(p, 0, sizeof(*p));
  p->used = 1;
  snprintf(p->name, sizeof p->name, "我的作品 %lu",
           (unsigned long)work_catalog.count + 1);
  if (example == 1) {
    strcpy(p->name, "电机与等待");
    p->nodes[0] = (Node){MOTOR, -1, 0, 10, 0};
    p->nodes[1] = (Node){WAIT, -1, 1, 10, 0};
  }
  if (example == 2) {
    strcpy(p->name, "重复动作");
    p->nodes[0] = (Node){LOOP, -1, 0, 3, 0};
    p->nodes[1] = (Node){MOTOR, 0, 0, 10, 0};
  }
  uint32_t id;
  if (!works_create(p, &id)) {
    say("新建失败，存储空间不足或写入失败");
    return;
  }
  library.items[slot] = *p;
  work_ids[slot] = id;
  home_page = SIZE_MAX;
  active_project = slot;
  memcpy(nodes, p->nodes, sizeof nodes);
  lv_async_call(editor_later, NULL);
}
static void delete_confirm(lv_event_t *e) {
  (void)e;
  if (!works_delete(work_ids[menu_project])) {
    close_modal(NULL);
    say("删除失败，作品已保留");
    return;
  }
  close_modal(NULL);
  lv_async_call(home_later, NULL);
}
static void rename_ready(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CANCEL) {
    close_modal(NULL);
    return;
  }
  const char *name = lv_textarea_get_text(name_input);
  if (!name[0])
    return;
  Project old = library.items[menu_project];
  snprintf(library.items[menu_project].name, 64, "%s", name);
  if (!works_save(work_ids[menu_project], &library.items[menu_project])) {
    library.items[menu_project] = old;
    close_modal(NULL);
    say("重命名失败");
    return;
  }
  close_modal(NULL);
  lv_async_call(home_later, NULL);
}
static void rename_project(lv_event_t *e) {
  (void)e;
  close_modal(NULL);
  modal = box(lv_screen_active(), 0, 0, 800, 480, 0xf1f5fa);
  lv_obj_t *heading = label(modal, "作品名称（键盘支持英文和数字）");
  place_label(heading, 20, 15);
  name_input = lv_textarea_create(modal);
  lv_obj_set_pos(name_input, 20, 50);
  lv_obj_set_size(name_input, 760, 55);
  lv_textarea_set_one_line(name_input, true);
  lv_textarea_set_max_length(name_input, 16);
  lv_textarea_set_text(name_input, library.items[menu_project].name);
  lv_obj_t *keys = lv_keyboard_create(modal);
  lv_obj_set_size(keys, 800, 350);
  lv_obj_align(keys, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(keys, name_input);
  lv_obj_add_event_cb(keys, rename_ready, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(keys, rename_ready, LV_EVENT_CANCEL, NULL);
}
static void duplicate_project(lv_event_t *e) {
  (void)e;
  close_modal(NULL);
  if (work_catalog.count >= WORK_LIMIT) {
    say("最多6个作品，请先删除一个");
    return;
  }
  Project copy = library.items[menu_project];
  snprintf(copy.name, 64, "作品副本 %lu",
           (unsigned long)work_catalog.count + 1);
  uint32_t id;
  if (!works_create(&copy, &id)) {
    say("复制失败，存储空间不足或写入失败");
    return;
  }
  home_page = SIZE_MAX;
  lv_async_call(home_later, NULL);
}
static void delete_project(lv_event_t *e) {
  (void)e;
  close_modal(NULL);
  modal = box(lv_screen_active(), 0, 0, 800, 480, 0x263744);
  lv_obj_set_style_bg_opa(modal, LV_OPA_50, 0);
  lv_obj_t *panel = box(modal, 150, 125, 500, 230, 0xe6eef8);
  lv_obj_t *title = label(panel, "删除此作品？");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_t *name = label(panel, library.items[menu_project].name);
  lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 65);
  button(panel, 30, 145, 180, 52, "取消", close_modal, 0);
  button(panel, 290, 145, 180, 52, "删除", delete_confirm, 0);
}
static void project_menu(lv_event_t *e) {
  menu_project = (int)(intptr_t)lv_event_get_user_data(e);
  modal = box(lv_screen_active(), 0, 0, 800, 480, 0x263744);
  lv_obj_set_style_bg_opa(modal, LV_OPA_50, 0);
  lv_obj_t *panel = box(modal, 150, 110, 500, 260, 0xe6eef8);
  lv_obj_t *title = label(panel, library.items[menu_project].name);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  button(panel, 25, 90, 140, 52, "重命名", rename_project, 0);
  button(panel, 180, 90, 140, 52, "复制", duplicate_project, 0);
  button(panel, 335, 90, 140, 52, "删除", delete_project, 0);
  button(panel, 160, 180, 180, 52, "取消", close_modal, 0);
}
static bool examples_view;
static void change_home_page(lv_event_t *e) {
  int direction = (int)(intptr_t)lv_event_get_user_data(e);
  if (direction < 0 && home_page)
    home_page--;
  if (direction > 0 && (home_page + 1) * PROJECTS < work_catalog.count)
    home_page++;
  lv_async_call(home_later, NULL);
}
static void change_home_view(lv_event_t *e) {
  examples_view = (int)(intptr_t)lv_event_get_user_data(e) != 0;
  lv_async_call(home_later, NULL);
}
static void home(void) {
  home_thumbnail_dispose();
  scroll_cache_dispose();
  execution_dispose();
  modal = NULL;
  status = NULL;
  lv_obj_clean(lv_screen_active());
  box(lv_screen_active(), 0, 0, 144, 480, 0xe9eff7);
  button(lv_screen_active(), 8, 80, 128, 52, "我的作品", change_home_view, 0);
  button(lv_screen_active(), 8, 148, 128, 52, "积木示例", change_home_view, 1);
  button(lv_screen_active(), 8, 216, 128, 52, "蓝牙设备", open_bluetooth, 0);
  lv_obj_t *local = label(lv_screen_active(), "保存在此设备");
  place_label(local, 15, 440);
  lv_obj_set_style_text_font(local, &board_font, 0);
  lv_obj_t *title = label(lv_screen_active(), "卡片编程");
  place_label(title, 168, 18);
  button(lv_screen_active(), 650, 8, 136, 48, "新建作品", create_project, 0);
  status = label(lv_screen_active(),
                 storage_ready ? (examples_view ? "选择示例创建独立作品"
                                                : "我的作品 · 点击打开")
                               : "存储读取失败，原数据未修改");
  place_label(status, 168, 58);
  if (!storage_ready)
    return;
  if (examples_view) {
    button(lv_screen_active(), 170, 110, 280, 120, "电机与等待", create_project,
           1);
    button(lv_screen_active(), 480, 110, 280, 120, "重复动作", create_project,
           2);
    return;
  }
  if (!works_catalog(&work_catalog)) {
    say("作品读取失败，原数据未修改");
    return;
  }
  size_t pages = (work_catalog.count + PROJECTS - 1) / PROJECTS;
  if (!pages)
    pages = 1;
  if (home_page >= pages)
    home_page = pages - 1;
  projects_empty(&library);
  for (int i = 0; i < PROJECTS; i++) {
    size_t index = home_page * PROJECTS + i;
    if (index >= work_catalog.count)
      break;
    work_ids[i] = work_catalog.items[index].id;
    if (!works_read(work_ids[i], &library.items[i])) {
      say("作品读取失败，原数据未修改");
      return;
    }
  }
  int ordinal = 0;
  for (int i = 0; i < PROJECTS; i++)
    if (library.items[i].used) {
      int x = 164 + (ordinal % 3) * 210, y = 100 + (ordinal / 3) * 180;
      ordinal++;
      lv_obj_t *card = box(lv_screen_active(), x, y, 198, 164, 0xe3efff);
      lv_obj_add_event_cb(card, enter_project, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
      lv_obj_t *placeholder = box(card, 8, 8, 182, 108, 0xeeeeee);
      lv_obj_remove_flag(placeholder, LV_OBJ_FLAG_CLICKABLE);
      label(placeholder, "正在加载");
      home_thumbnail_cards[i] = card;
      lv_obj_t *name = label(card, library.items[i].name);
      place_label(name, 10, 130);
      lv_obj_set_width(name, 132);
      lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
      button(card, 146, 120, 48, 44, "...", project_menu, i);
    }
  if (ordinal)
    home_thumbnail_timer = lv_timer_create(home_thumbnail_load_next, 16, NULL);
  if (!ordinal) {
    lv_obj_t *empty = label(lv_screen_active(), "还没有作品，点击新建作品开始");
    place_label(empty, 210, 240);
  }
  if (pages > 1) {
    lv_obj_t *prev = button(lv_screen_active(), 390, 446, 110, 32, "上一页",
                            change_home_page, -1);
    lv_obj_t *next = button(lv_screen_active(), 666, 446, 110, 32, "下一页",
                            change_home_page, 1);
    if (!home_page)
      lv_obj_add_state(prev, LV_STATE_DISABLED);
    if (home_page + 1 == pages)
      lv_obj_add_state(next, LV_STATE_DISABLED);
    char text[48];
    snprintf(text, sizeof text, "%lu / %lu", (unsigned long)home_page + 1,
             (unsigned long)pages);
    lv_obj_t *page = label(lv_screen_active(), text);
    place_label(page, 550, 452);
  }
}
void editor_create(void) {
  home_thumb_cache_clear();
  lv_obj_set_style_text_font(lv_screen_active(), &board_font, 0);
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0x263744), 0);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xf5f7fb), 0);
  storage_ready = works_open() >= 0;
  home_page = 0;
  active_project = -1;
  examples_view = false;
  home();
}
