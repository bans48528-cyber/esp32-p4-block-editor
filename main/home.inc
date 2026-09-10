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
  if (projects_save(&library))
    return true;
  library.items[active_project] = old;
  return false;
}
static void save_work(lv_event_t *e) {
  (void)e;
  say(save_active() ? "作品已保存" : "保存失败，请重试");
}
static void back_home(lv_event_t *e) {
  (void)e;
  if (!save_active()) {
    say("保存失败，已保留编辑内容");
    return;
  }
  lv_async_call(home_later, NULL);
}
static int free_project(void) {
  for (int i = 0; i < PROJECTS; i++)
    if (!library.items[i].used)
      return i;
  return -1;
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
  int slot = free_project();
  if (slot < 0) {
    say("最多六个作品，请先删除一个");
    return;
  }
  int example = (int)(intptr_t)lv_event_get_user_data(e);
  Project *p = &library.items[slot];
  memset(p, 0, sizeof(*p));
  p->used = 1;
  snprintf(p->name, sizeof p->name, "我的作品 %d", slot + 1);
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
  if (!projects_save(&library)) {
    memset(p, 0, sizeof(*p));
    say("新建失败，请重试");
    return;
  }
  active_project = slot;
  memcpy(nodes, p->nodes, sizeof nodes);
  lv_async_call(editor_later, NULL);
}
static void delete_confirm(lv_event_t *e) {
  (void)e;
  Project old = library.items[menu_project];
  memset(&library.items[menu_project], 0, sizeof(Project));
  if (!projects_save(&library)) {
    library.items[menu_project] = old;
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
  if (!projects_save(&library)) {
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
  int slot = free_project();
  close_modal(NULL);
  if (slot < 0) {
    say("最多六个作品，请先删除一个");
    return;
  }
  library.items[slot] = library.items[menu_project];
  snprintf(library.items[slot].name, 64, "作品副本 %d", slot + 1);
  if (!projects_save(&library)) {
    memset(&library.items[slot], 0, sizeof(Project));
    say("复制失败");
    return;
  }
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
static void change_home_view(lv_event_t *e) {
  examples_view = (int)(intptr_t)lv_event_get_user_data(e) != 0;
  lv_async_call(home_later, NULL);
}
static void home(void) {
  modal = NULL;
  status = NULL;
  lv_obj_clean(lv_screen_active());
  box(lv_screen_active(), 0, 0, 144, 480, 0xe9eff7);
  button(lv_screen_active(), 8, 80, 128, 52, "我的作品", change_home_view, 0);
  button(lv_screen_active(), 8, 148, 128, 52, "积木示例", change_home_view, 1);
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
  int ordinal = 0;
  for (int i = 0; i < PROJECTS; i++)
    if (library.items[i].used) {
      int x = 164 + (ordinal % 3) * 210, y = 100 + (ordinal / 3) * 180;
      ordinal++;
      lv_obj_t *card = box(lv_screen_active(), x, y, 198, 164, 0xe3efff);
      lv_obj_add_event_cb(card, enter_project, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
      lv_obj_t *name = label(card, library.items[i].name);
      place_label(name, 10, 105);
      lv_obj_set_width(name, 178);
      lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
      int all = 0, preview = 0;
      for (int j = 0; j < LIMIT; j++)
        if (library.items[i].nodes[j].type) {
          all++;
          if (preview < 4) {
            lv_obj_t *tile = box(card, 12 + preview * 40, 50, 34, 40,
                                 blocks[library.items[i].nodes[j].type].color);
            lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
            preview++;
          }
        }
      char text[24];
      snprintf(text, sizeof text, "%d 张卡片", all);
      lv_obj_t *detail = label(card, text);
      place_label(detail, 10, 135);
      button(card, 140, 2, 56, 44, "...", project_menu, i);
    }
  if (!ordinal) {
    lv_obj_t *empty = label(lv_screen_active(), "还没有作品，点击新建作品开始");
    place_label(empty, 210, 240);
  }
}
void editor_create(void) {
  lv_obj_set_style_text_font(lv_screen_active(), &board_font, 0);
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0x263744), 0);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xf5f7fb), 0);
  storage_ready = projects_load(&library) >= 0;
  active_project = -1;
  examples_view = false;
  home();
}
