#include "ble_scan.h"
#include "block_shape.h"
#include "editor.h"
#include "projects.h"
#include "thumbnails.h"
#include <assert.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
static uint32_t pixels[800 * 480];
static uint32_t drawbuf[800 * 480];
static lv_point_t point;
static bool down;
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *p) {
  uint32_t *src = (uint32_t *)p;
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++)
      pixels[y * 800 + x] = *src++;
  lv_display_flush_ready(d);
}
static void read_pointer(lv_indev_t *d, lv_indev_data_t *data) {
  (void)d;
  data->point = point;
  data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void step(void) {
  for (int i = 0; i < 3; i++) {
    lv_tick_inc(20);
    lv_timer_handler();
  }
}
static void press(int x, int y, bool held) {
  point.x = x;
  point.y = y;
  down = held;
  step();
}
static void tap(int x, int y) {
  press(x, y, true);
  press(x, y, false);
}
static void drag(int x, int y, int tx, int ty) {
  press(x, y, true);
  for (int i = 1; i <= 15; i++)
    press(x + (tx - x) * i / 15, y + (ty - y) * i / 15, true);
  press(tx, ty, false);
}
static void snapshot(const char *path) {
  step();
  FILE *f = fopen(path, "wb");
  assert(f);
  fprintf(f, "P6\n800 480\n255\n");
  for (int i = 0; i < 800 * 480; i++) {
    unsigned char rgb[] = {(pixels[i] >> 16) & 255, (pixels[i] >> 8) & 255,
                           pixels[i] & 255};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}
static bool has_text(lv_obj_t *o, const char *text) {
  if (lv_obj_check_type(o, &lv_label_class) &&
      strcmp(lv_label_get_text(o), text) == 0)
    return true;
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
    if (has_text(lv_obj_get_child(o, i), text))
      return true;
  return false;
}
static lv_obj_t *find_class(lv_obj_t *o, const lv_obj_class_t *cls) {
  if (lv_obj_check_type(o, cls))
    return o;
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
    lv_obj_t *found = find_class(lv_obj_get_child(o, i), cls);
    if (found)
      return found;
  }
  return NULL;
}

static lv_obj_t *find_text(lv_obj_t *o, const char *text) {
  if (lv_obj_check_type(o, &lv_label_class) &&
      strcmp(lv_label_get_text(o), text) == 0)
    return o;
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
    lv_obj_t *f = find_text(lv_obj_get_child(o, i), text);
    if (f)
      return f;
  }
  return NULL;
}
static void select_label_gesture(const char *text) {
  lv_obj_t *o = find_text(lv_screen_active(), text);
  assert(o);
  lv_area_t r;
  lv_obj_get_coords(o, &r);
  int x = (r.x1 + r.x2) / 2;
  press(x, 280, true);
  for (int i = 0; i < 10; i++)
    step();
  press(x + 1, (r.y1 + r.y2) / 2, true);
  press(x + 1, (r.y1 + r.y2) / 2, false);
}
static void pull_staged(void) {
  press(90, 400, true);
  press(90, 360, true);
  press(450, 160, true);
  press(450, 160, false);
}
static void click_text(const char *text) {
  step();
  lv_obj_t *o = find_text(lv_screen_active(), text);
  if (!o) {
    const char *actions[] = {"主页", "撤销", "前进", "保存", "清空"};
    for (int i = 0; i < 5; i++)
      if (!strcmp(text, actions[i])) {
        tap(i == 0 ? 32 : 544 + 56 * i, 27);
        return;
      }
    printf("Missing text: %s\n", text);
    fflush(stdout);
    snapshot("failure.ppm");
    exit(2);
  }
  lv_area_t r;
  lv_obj_get_coords(o, &r);
  tap((r.x1 + r.x2) / 2, (r.y1 + r.y2) / 2);
}
static void model_tests(void) {
  Node n[LIMIT] = {0};
  for (int t = 1; t <= TYPES; t++)
    assert(model_add(n, t, -1, model_count(n, -1)) >= 0);
  assert(model_valid(n));
  memset(n, 0, sizeof n);
  int loop = model_add(n, LOOP, -1, 0), inner = model_add(n, FOREVER, loop, 0),
      motor = model_add(n, REVERSE, inner, 0);
  assert(model_valid(n));
  bool selected[LIMIT] = {0};
  selected[loop] = true;
  Node backup[LIMIT];
  memcpy(backup, n, sizeof n);
  assert(!model_move(n, selected, inner, 0));
  assert(memcmp(backup, n, sizeof n) == 0);
  selected[motor] = true;
  assert(model_move(n, selected, -2, 0));
  assert(n[loop].parent == -2 && n[motor].parent == inner);
  assert(model_move(n, selected, -1, 0));
  model_delete(n, selected);
  assert(model_count(n, -1) == 0);
  for (int i = 0; i < LIMIT; i++)
    assert(!n[i].type);
  for (int i = 0; i < LIMIT; i++)
    assert(model_add(n, WAIT, -1, i) >= 0);
  assert(model_add(n, WAIT, -1, LIMIT) < 0);
  assert(model_valid(n));
  memset(selected, 0, sizeof selected);
  selected[1] = selected[3] = true;
  assert(model_move(n, selected, -1, 7));
  assert(n[1].order == 5 && n[3].order == 6);
  assert(model_valid(n));
  memset(n, 0, sizeof n);
  int parent = -1;
  for (int i = 0; i < MAX_DEPTH; i++) {
    parent = model_add(n, LOOP, parent, 0);
    assert(parent >= 0);
  }
  assert(model_add(n, MOTOR, parent, 0) < 0);
  assert(model_valid(n));
  memset(n, 0, sizeof n);
  for (int i = 0; i < 20; i++)
    model_add(n, i % TYPES + 1, -1, i);
  srand(27);
  for (int k = 0; k < 2000; k++) {
    memcpy(backup, n, sizeof n);
    for (int i = 0; i < LIMIT; i++)
      selected[i] = n[i].type && rand() % 5 == 0;
    int target = -1;
    if (rand() % 2) {
      int candidate = rand() % 20;
      if (is_loop(n[candidate].type))
        target = candidate;
    }
    int pos = rand() % (model_count(n, target) + 1);
    bool ok = model_move(n, selected, target, pos);
    assert(model_valid(n));
    if (!ok)
      assert(!memcmp(backup, n, sizeof n));
    for (int i = 0; i < LIMIT; i++) {
      assert(n[i].type == backup[i].type);
      assert(n[i].value == backup[i].value);
    }
  }
  typedef struct {
    int32_t type, parent, order, value, port;
  } OldNode;
  typedef struct {
    uint32_t used;
    char name[64];
    OldNode n[24];
  } OldProject;
  typedef struct {
    uint32_t magic, version;
    OldProject p[6];
  } OldLibrary;
  OldLibrary old = {0};
  old.magic = 0x43505231;
  old.version = 1;
  old.p[0].used = 1;
  strcpy(old.p[0].name, "Legacy");
  old.p[0].n[0] = (OldNode){MOTOR, -1, 0, 3, 1};
  Library migrated;
  assert(projects_decode(&migrated, &old, sizeof old));
  assert(migrated.version == 3 && migrated.items[0].nodes[0].value == 30);
  old.p[0].n[0].parent = 0;
  assert(!projects_decode(&migrated, &old, sizeof old));
  assert(projects_valid(&migrated));
}
#include "work_store_fixture.h"
static void press_palette_motor_stop(void) {
  lv_obj_t *edge_palette_item = lv_obj_get_parent(lv_obj_get_parent(
      find_text(lv_screen_active(), "电机停止")));
  lv_area_t edge_palette_bounds;
  lv_obj_get_coords(edge_palette_item, &edge_palette_bounds);
  int edge_palette_x = (edge_palette_bounds.x1 + edge_palette_bounds.x2) / 2;
  int edge_palette_y = edge_palette_bounds.y1 + 30;
  press(edge_palette_x, edge_palette_y, true);
}
int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "--verify-thumbnails")) {
    _putenv_s("BOARD_PROJECTS_FILE", argv[2]);
    Library saved;
    if (projects_load(&saved) != 1)
      return 3;
    uint8_t *data = malloc(THUMB_BYTES);
    if (!data)
      return 4;
    for (int i = 0; i < PROJECTS; i++)
      if (saved.items[i].used &&
          !thumbnail_load(i, saved.items[i].nodes, data)) {
        free(data);
        return 5;
      }
    free(data);
    return 0;
  }
  model_tests();
  BleScanSnapshot scan = {0};
  uint8_t addr[6] = {1, 2, 3, 4, 5, 6};
  uint8_t service[] = {3, 3, 0xf0, 0xff},
          name[] = {9, 9, 'S', 'p', 'a', 'r', 'k', '_', 'A', 'I'};
  assert(ble_scan_record(&scan, addr, 0, -60, service, sizeof service));
  assert(ble_scan_record(&scan, addr, 0, -45, name, sizeof name));
  assert(scan.count == 1 && scan.devices[0].spark &&
         scan.devices[0].service_match && scan.devices[0].rssi == -45);
  uint8_t broken_ad[] = {12, 9, 'a'};
  assert(!ble_scan_record(&scan, addr, 0, -30, broken_ad, sizeof broken_ad));
  assert(scan.devices[0].rssi == -45);
  assert(ble_scan_record(&scan, addr, 1, -60, name, sizeof name));
  assert(scan.count == 2);
  for (int i = 0; i < 40; i++) {
    addr[0] = (uint8_t)(10 + i);
    ble_scan_record(&scan, addr, 0, -70, name, sizeof name);
  }
  assert(scan.count == BLE_SCAN_CAPACITY);
  puts("PASS: BLE advertising parser, scan-response merge, malformed packet "
       "rejection, bounded device list");
  char testfile[80];
  snprintf(testfile, sizeof testfile, "test-projects-%lu.bin",
           GetCurrentProcessId());
  _putenv_s("BOARD_PROJECTS_FILE", testfile);
  lv_init();
  lv_display_t *d = lv_display_create(800, 480);
  lv_display_set_flush_cb(d, flush);
  lv_display_set_buffers(d, drawbuf, NULL, sizeof drawbuf,
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_indev_t *input = lv_indev_create();
  lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(input, read_pointer);
  editor_create();
  step();
  click_text("蓝牙设备");
  click_text("搜索设备");
  assert(has_text(lv_screen_active(), "电脑预览不扫描蓝牙，请在屏幕板上搜索"));
  snapshot("bluetooth-preview.ppm");
  click_text("返回");
  step();
  click_text("新建作品");
  assert(has_text(lv_screen_active(), "设备未连接"));
  snapshot("editor-empty.ppm");
  assert(!has_text(lv_screen_active(), "多选"));
  assert(!has_text(lv_screen_active(), "删除"));
  click_text("电机正转");
  assert(has_text(lv_screen_active(), "E 1.0s"));
  click_text("E 1.0s");
  assert(!has_text(lv_screen_active(), "退格"));
  click_text("+");
  assert(has_text(lv_screen_active(), "1.1"));
  click_text("-");
  snapshot("editor-parameter-popover.ppm");
  lv_area_t bubble_r, option_r;
  lv_obj_get_coords(find_text(lv_screen_active(), "E 1.0s"), &bubble_r);
  lv_obj_get_coords(find_text(lv_screen_active(), "E"), &option_r);
  assert(option_r.y1 > bubble_r.y2);

  click_text("1.0");
  snapshot("editor-keypad.ppm");
  click_text("2");
  click_text(".");
  click_text("5");
  click_text("F");
  click_text("确定");
  assert(has_text(lv_screen_active(), "F 2.5s"));
  click_text("撤销");
  assert(has_text(lv_screen_active(), "E 1.0s"));
  click_text("前进");
  assert(has_text(lv_screen_active(), "F 2.5s"));
  click_text("逻辑");
  click_text("次数");
  snapshot("editor-loop.ppm");
  /* Native pointer drag from the wait palette into the empty loop cavity. */
  press(60, 400, true);
  press(60, 360, true);
  press(205, 350, true);
  for (int y = 335; y >= 181; y -= 14)
    press(205, y, true);
  press(205, 181, true);
  press(205, 181, false);
  if (!has_text(lv_screen_active(), "已放入循环")) {
    snapshot("projection-failure.ppm");
    puts("FAIL loop insertion");
    return 4;
  }
  snapshot("editor-loop-child.ppm");
  click_text("保存");
  Library l;
  assert(projects_load(&l) == 1);
  int loops = 0, nested = 0;
  for (int i = 0; i < LIMIT; i++) {
    loops += is_loop(l.items[0].nodes[i].type);
    nested += l.items[0].nodes[i].type && l.items[0].nodes[i].parent >= 0;
  }
  assert(loops == 1 && nested == 1);
  click_text("声光");
  click_text("点阵");
  click_text("绘制");
  drag(204, 124, 396, 316);
  snapshot("editor-matrix.ppm");
  click_text("确定");
  click_text("保存");
  assert(projects_load(&l) == 1);
  int matrix = -1;
  for (int i = 0; i < LIMIT; i++)
    if (l.items[0].nodes[i].type == MATRIX)
      matrix = i;
  assert(matrix >= 0);
  assert(l.items[0].nodes[matrix].matrix & (1u << 0));
  assert(l.items[0].nodes[matrix].matrix & (1u << 24));
  select_label_gesture("F 2.5s");
  select_label_gesture("绘制");
  press(130, 166, true);
  press(530, 390, true);
  for (int i = 0; i < 22; i++)
    step();
  press(530, 390, false);
  assert(has_text(lv_screen_active(), "组合 1 · 2 张"));
  snapshot("editor-staged.ppm");
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(model_count(l.items[0].nodes, -2) == 2);
  /* Tapping or horizontal browsing never takes a group out of staging. */
  click_text("组合 1 · 2 张");
  drag(90, 400, 150, 400);
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(model_count(l.items[0].nodes, -2) == 2);
  pull_staged();
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(model_count(l.items[0].nodes, -2) == 0);
  click_text("清空");
  tap(530, 288);
  assert(has_text(lv_screen_active(), "从下方拖入积木开始编程"));
  click_text("撤销");
  assert(has_text(lv_screen_active(), "F 2.5s"));
  click_text("主页");
  editor_create();
  step();
  tap(220, 175);
  assert(has_text(lv_screen_active(), "F 2.5s"));
  /* Seed representative nested structure to audit baseline and parameter
   * overlap. */
  click_text("主页");
  assert(projects_load(&l) == 1);
  Node *n = l.items[0].nodes;
  memset(n, 0, sizeof l.items[0].nodes);
  int outer = model_add(n, LOOP, -1, 0),
      inner = model_add(n, FOREVER, outer, 0);
  model_add(n, MOTOR, inner, 0);
  model_add(n, WAIT, inner, 1);
  model_add(n, MATRIX, outer, 1);
  model_add(n, ULTRASONIC, -1, 1);
  model_add(n, GRAYSCALE, -1, 2);
  model_add(n, NOTE, -1, 3);
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  snapshot("editor-nested.ppm");
  click_text("组合电机");
  snapshot("editor-combo.ppm");
  click_text("传感");
  snapshot("editor-sensors.ppm");
  click_text("声光");
  snapshot("editor-light.ppm");
  for (int i = 0; i < 20; i++) {
    tap(650, 280);
    click_text("电机");
    click_text("逻辑");
  }
  /* Long labels are displaced without separating the mating block bodies. */
  click_text("主页");
  assert(projects_load(&l) == 1);
  memset(l.items[0].nodes, 0, sizeof l.items[0].nodes);
  for (int i = 0; i < 5; i++) {
    int id = model_add(l.items[0].nodes, MOTOR, -1, i);
    l.items[0].nodes[id].value = 9999;
  }
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  snapshot("editor-long-parameters.ppm");
  /* Bounds rejected; cancel leaves the original parameter unchanged. */
  click_text("E 999.9s");
  click_text("999.9");
  click_text("0");
  click_text("确定");
  assert(has_text(lv_screen_active(), "数值超出范围"));
  click_text("取消");
  /* Single-block delete by a short drop into the lower area, then undo. */
  drag(130, 155, 500, 385);
  assert(has_text(lv_screen_active(), "已删除，可撤销"));
  click_text("撤销");
  /* A blank tap cancels selection; even a long library hover of one
   * ordinary block deletes it instead of creating a staged group. */
  press(105, 100, true);
  for (int i = 0; i < 10; i++)
    step();
  press(235, 185, true);
  press(235, 185, false);
  tap(650, 280);
  press(125, 155, true);
  press(500, 390, true);
  for (int i = 0; i < 22; i++)
    step();
  press(500, 390, false);
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(model_count(l.items[0].nodes, -1) == 4);
  assert(model_count(l.items[0].nodes, -2) == 0);
  click_text("撤销");
  /* Long-hold rectangle selection, group staging by drag, and drag restoration.
   */
  press(105, 100, true);
  for (int i = 0; i < 10; i++)
    step();
  press(235, 185, true);
  press(235, 185, false);
  snapshot("editor-common-selection.ppm");
  press(125, 155, true);
  for (int i = 1; i <= 15; i++)
    press(125 + (530 - 125) * i / 15, 155 + (390 - 155) * i / 15, true);
  for (int i = 0; i < 22; i++)
    step();
  press(530, 390, false);
  assert(has_text(lv_screen_active(), "组合 1 · 2 张"));
  pull_staged();
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(model_count(l.items[0].nodes, -2) == 0);
  assert(model_count(l.items[0].nodes, -1) == 5);
  /* A numeric popup near the right edge keeps every keypad key on screen. */
  click_text("主页");
  assert(projects_load(&l) == 1);
  memset(l.items[0].nodes, 0, sizeof l.items[0].nodes);
  for (int i = 0; i < 8; i++) {
    int id = model_add(l.items[0].nodes, MOTOR, -1, i);
    l.items[0].nodes[id].port = i == 7 ? 3 : 0;
  }
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  click_text("H 1.0s");
  click_text("1.0");
  snapshot("editor-keypad-right.ppm");
  const char *visible_keys[] = {"1", "9", "退格", "清除", "收起", "确定"};
  for (int i = 0; i < 6; i++) {
    lv_obj_t *key = find_text(lv_screen_active(), visible_keys[i]);
    assert(key);
    lv_area_t r;
    lv_obj_get_coords(key, &r);
    assert(r.x1 >= 0 && r.x2 < 800 && r.y1 >= 0 && r.y2 < 480);
  }
  click_text("2");
  click_text("收起");
  assert(!has_text(lv_screen_active(), "退格"));
  tap(790, 470);
  assert(!has_text(lv_screen_active(), "确定"));
  assert(has_text(lv_screen_active(), "H 1.0s"));
  /* Regression: both plugs must fill the sockets, not be clipped at the parent
   * edge. */
  click_text("主页");
  assert(projects_load(&l) == 1);
  memset(l.items[0].nodes, 0, sizeof l.items[0].nodes);
  int connector_loop = model_add(l.items[0].nodes, FOREVER, -1, 0);
  model_add(l.items[0].nodes, MOTOR, connector_loop, 0);
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  snapshot("editor-connectors.ppm");
  unsigned start_tip = pixels[166 * 800 + 95] & 0xffffff,
           motor_tip = pixels[166 * 800 + 197] & 0xffffff;
  printf("Connector centers: start=%06x motor=%06x\n", start_tip, motor_tip);
  if (start_tip != 0xf5c402 || motor_tip != 0x0090f5) {
    puts("FAIL: a connector is clipped");
    return 3;
  }
  /* Full-volume insertion preview pushes the following block by one advance. */
  click_text("主页");
  assert(projects_load(&l) == 1);
  memset(l.items[0].nodes, 0, sizeof l.items[0].nodes);
  model_add(l.items[0].nodes, MOTOR, -1, 0);
  model_add(l.items[0].nodes, MOTOR, -1, 1);
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  click_text("传感");
  press(208, 400, true);
  press(180, 350, true);
  press(180, 220, true);
  snapshot("editor-projection.ppm");
  unsigned projection_color = pixels[145 * 800 + 185] & 0xffffff,
           next_color = pixels[145 * 800 + 254] & 0xffffff;
  printf("Projection fill=%06x displaced block=%06x\n", projection_color,
         next_color);
  assert(projection_color != 0xeeeeee && projection_color != 0x0090f5);
  assert(next_color == 0x0090f5);
  Library unchanged;
  assert(projects_load(&unchanged) == 1);
  assert(model_count(unchanged.items[0].nodes, -1) == 2);
  press(180, 220, false);
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(model_count(l.items[0].nodes, -1) == 3);
  assert(l.items[0].nodes[model_nth(l.items[0].nodes, -1, 1)].type ==
         GRAYSCALE);
  Node before_cancel[LIMIT];
  memcpy(before_cancel, l.items[0].nodes, sizeof before_cancel);
  press(130, 155, true);
  press(290, 180, true);
  press(290, 30, true);
  press(290, 30, false);
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(!memcmp(before_cancel, l.items[0].nodes, sizeof before_cancel));
  click_text("撤销");
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(model_count(l.items[0].nodes, -1) == 2);
  click_text("逻辑");
  press(220, 405, true);
  press(180, 350, true);
  press(180, 210, true);
  snapshot("editor-loop-projection.ppm");
  assert((pixels[145 * 800 + 318] & 0xffffff) == 0x0090f5);
  press(180, 30, true);
  press(180, 30, false);
  press(100, 100, true);
  for (int i = 0; i < 10; i++)
    step();
  press(235, 185, true);
  press(235, 185, false);
  press(130, 160, true);
  press(400, 180, true);
  snapshot("editor-group-projection.ppm");
  unsigned group_a = pixels[145 * 800 + 102] & 0xffffff,
           group_b = pixels[145 * 800 + 182] & 0xffffff;
  assert(group_a != 0xeeeeee && group_a != 0x0090f5);
  assert(group_b == group_a);
  press(400, 30, true);
  press(400, 30, false);
  /* A taller loop must keep the exposed right edge above its next block. */
  click_text("主页");
  assert(projects_load(&l) == 1);
  memset(l.items[0].nodes, 0, sizeof l.items[0].nodes);
  model_add(l.items[0].nodes, FOREVER, -1, 0);
  model_add(l.items[0].nodes, MOTOR_STOP, -1, 1);
  model_add(l.items[0].nodes, FOREVER, -1, 2);
  model_add(l.items[0].nodes, LEFT, -1, 3);
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  snapshot("editor-loop-edge.ppm");
  unsigned exposed_edge = pixels[123 * 800 + 233] & 0xffffff;
  assert(exposed_edge != blocks[FOREVER].color && exposed_edge != 0xeeeeee);
  /* Maximum valid nesting and node count also exercise image allocation. */
  click_text("主页");
  assert(projects_load(&l) == 1);
  memset(l.items[0].nodes, 0, sizeof l.items[0].nodes);
  int parent = -1;
  for (int i = 0; i < MAX_DEPTH - 1; i++)
    parent = model_add(l.items[0].nodes, LOOP, parent, 0);
  for (int i = MAX_DEPTH - 1; i < LIMIT; i++)
    assert(model_add(l.items[0].nodes, MOTOR, parent,
                     model_count(l.items[0].nodes, parent)) >= 0);
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  snapshot("editor-capacity.ppm");
  assert(projects_valid(&l));
  /* Existing programs receive real persisted previews without rewriting data.
   */
  projects_empty(&l);
  for (int i = 0; i < 3; i++) {
    l.items[i].used = 1;
    snprintf(l.items[i].name, 64, "我的作品 %d", i + 1);
  }
  model_add(l.items[0].nodes, MOTOR, -1, 0);
  model_add(l.items[0].nodes, WAIT, -1, 1);
  int loop = model_add(l.items[1].nodes, LOOP, -1, 0);
  model_add(l.items[1].nodes, MOTOR, loop, 0);
  model_add(l.items[1].nodes, REVERSE, loop, 1);
  model_add(l.items[2].nodes, BUTTON_SENSOR, -1, 0);
  model_add(l.items[2].nodes, MATRIX, -1, 1);
  assert(projects_save(&l));
  editor_create();
  step();
  uint8_t *before = malloc(THUMB_BYTES), *after = malloc(THUMB_BYTES);
  assert(before && after);
  assert(thumbnail_load(0, l.items[0].nodes, before));
  snapshot("home-thumbnails.ppm");
  tap(220, 175);
  click_text("电机反转");
  click_text("保存");
  assert(has_text(lv_screen_active(), "作品已保存"));
  Library changed;
  assert(projects_load(&changed) == 1);
  assert(thumbnail_load(0, changed.items[0].nodes, after));
  assert(memcmp(before, after, THUMB_BYTES) != 0);
  assert(!thumbnail_load(0, l.items[0].nodes, after));
  click_text("主页");
  editor_create();
  step();
  assert(thumbnail_load(0, changed.items[0].nodes, after));
  snapshot("home-thumbnails-restarted.ppm");
  char executable[MAX_PATH];
  assert(GetModuleFileNameA(NULL, executable, sizeof executable));
  assert(_spawnl(_P_WAIT, executable, executable, "--verify-thumbnails",
                 testfile, NULL) == 0);
  /* A truncated image is repaired from saved nodes, leaving the program intact.
   */
  char thumbfile[MAX_PATH];
  snprintf(thumbfile, sizeof thumbfile, "%s.thumb-0.bin", testfile);
  FILE *badthumb = fopen(thumbfile, "wb");
  assert(badthumb);
  fwrite("bad", 1, 3, badthumb);
  fclose(badthumb);
  assert(!thumbnail_load(0, changed.items[0].nodes, after));
  editor_create();
  step();
  assert(thumbnail_load(0, changed.items[0].nodes, after));
  assert(projects_load(&l) == 1);
  assert(!memcmp(&l, &changed, sizeof l));
  free(before);
  free(after);
  puts("PASS: thumbnail auto-generation, save update, separate-process "
       "restart, stale rejection and corruption recovery");
  /* New reference blocks keep old IDs and binary Node layout intact. */
  projects_empty(&l);
  l.items[0].used = 1;
  strcpy(l.items[0].name, "我的作品 1");
  const int added[] = {MOTOR_POWER, MOTOR_CONTINUOUS, REVERSE_CONTINUOUS,
                       COMBO_POWER, COMBO_CONTINUOUS, NOTE,
                       ULTRASONIC};
  for (int i = 0; i < 7; i++)
    assert(model_add(l.items[0].nodes, added[i], -1, i) == i);
  assert(l.items[0].nodes[0].option == 1);
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  snapshot("editor-new-blocks.ppm");
  /* Fixed controls must not bubble into selection or drag gestures. */
  tap(757, 301);
  assert(has_text(lv_screen_active(),
                  "电脑预览：下载并运行需在屏幕板连接蓝牙后使用"));
  tap(691, 301);
  assert(
      has_text(lv_screen_active(), "电脑预览：停止需在屏幕板连接蓝牙后使用"));
  snapshot("editor-execution-controls.ppm");
  assert((pixels[301 * 800 + 740] & 0xffffff) == 0xffd600);
  assert((pixels[301 * 800 + 758] & 0xffffff) == 0xffffff);
  assert((pixels[301 * 800 + 691] & 0xffffff) == 0xffffff);

  click_text("50%");
  snapshot("editor-power-options.ppm");
  click_text("100%");
  tap(750, 295);
  click_text("前进∞");
  click_text("右转");
  tap(750, 295);
  click_text("1·0.25拍");
  assert(has_text(lv_screen_active(), "音符") &&
         has_text(lv_screen_active(), "拍数"));
  click_text("6");
  click_text("7");
  snapshot("editor-note-beats.ppm");
  click_text("2拍");
  tap(750, 295);
  click_text("A<10cm");
  snapshot("editor-sensor-port.ppm");
  click_text("D");
  click_text(">");
  tap(750, 295);
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(l.items[0].nodes[0].option == 3);
  assert(l.items[0].nodes[4].option == 3);
  assert(l.items[0].nodes[5].value == 3 && l.items[0].nodes[5].option == 6);
  assert(l.items[0].nodes[6].port == 3 && l.items[0].nodes[6].option == 1);
  /* Horizontal palette browsing must not insert or delete blocks. */
  click_text("组合电机");
  drag(700, 400, 200, 400);
  click_text("保存");
  assert(projects_load(&changed) == 1 &&
         !memcmp(l.items[0].nodes, changed.items[0].nodes,
                 sizeof l.items[0].nodes));
  snapshot("editor-combo-palette-scroll.ppm");
  editor_create();
  step();
  tap(220, 175);
  assert(has_text(lv_screen_active(), "右转∞"));
  assert(has_text(lv_screen_active(), "7·2拍"));
  assert(has_text(lv_screen_active(), "D>10cm"));
  assert(sizeof(Node) == 28 && MOTOR == 1 && MATRIX == 17 && TYPES == 23);
  puts("PASS: five new blocks, power/direction/port/beats parameters, palette "
       "swipe and restart persistence");
  /* Version 2 -> 3 preserves all 32 slots and leaves the new slots empty. */
  struct V2Project {
    uint32_t used;
    char name[64];
    Node nodes[32];
  };
  struct V2Library {
    uint32_t magic, version;
    struct V2Project items[PROJECTS];
  } v2 = {0};
  v2.magic = l.magic;
  v2.version = 2;
  v2.items[0].used = 1;
  strcpy(v2.items[0].name, "旧作品");
  for (int i = 0; i < 32; i++) {
    v2.items[0].nodes[i] = node_default(WAIT);
    v2.items[0].nodes[i].order = i;
  }
  assert(projects_decode(&l, &v2, sizeof v2));
  assert(l.version == 3 && model_count(l.items[0].nodes, -1) == 32 &&
         !l.items[0].nodes[99].type);
  for (int i = 32; i < 100; i++)
    assert(model_add(l.items[0].nodes, WAIT, -1, i) == i);
  assert(model_add(l.items[0].nodes, WAIT, -1, 100) == -1);
  assert(projects_save(&l));
  assert(projects_load(&changed) == 1);
  assert(!memcmp(&l, &changed, sizeof l));
  editor_create();
  step();
  tap(220, 175);
  snapshot("editor-100-blocks.ppm");
  projects_empty(&l);
  l.items[0].used = 1;
  strcpy(l.items[0].name, "组合电机方向");
  model_add(l.items[0].nodes, COMBO_DIRECTION, -1, 0);
  model_add(l.items[0].nodes, FORWARD, -1, 1);
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  click_text("全部正向");
  snapshot("editor-combo-direction.ppm");
  click_text("左电机反向");
  click_text("右电机反向");
  click_text("全部反向");
  tap(600, 280);
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(l.items[0].nodes[0].option == 3);
  editor_create();
  step();
  tap(220, 175);
  assert(has_text(lv_screen_active(), "全部反向"));
  snapshot("editor-combo-direction-reversed.ppm");
  puts("PASS: V2 32-slot migration, 100-block save/load/limit/render, "
       "direction options and restart persistence");
  projects_empty(&l);
  l.items[0].used = 1;
  strcpy(l.items[0].name, "scroll cache");
  for (int i = 0; i < 20; i++)
    model_add(l.items[0].nodes, WAIT, -1, i);
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  for (int i = 0; i < 8; i++)
    step();
  lv_obj_t *cache_scene = lv_obj_get_parent(
      lv_obj_get_parent(find_text(lv_screen_active(), "1.0s")));
  lv_obj_t *cache_canvas = lv_obj_get_parent(cache_scene);
  static uint32_t before_cache[800 * 480];
  step();
  memcpy(before_cache, pixels, sizeof pixels);
  lv_obj_send_event(cache_canvas, LV_EVENT_SCROLL_BEGIN, NULL);
  step();
  assert(lv_obj_has_flag(cache_scene, LV_OBJ_FLAG_HIDDEN));
  assert(!memcmp(before_cache, pixels, sizeof pixels));
  lv_obj_send_event(cache_canvas, LV_EVENT_SCROLL_END, NULL);
  step();
  assert(!lv_obj_has_flag(cache_scene, LV_OBJ_FLAG_HIDDEN));
  assert(!memcmp(before_cache, pixels, sizeof pixels));
  press(450, 280, true);
  press(350, 280, true);
  press(200, 280, true);
  step();
  assert(lv_obj_get_scroll_x(cache_canvas) > 0);
  assert(lv_obj_has_flag(cache_scene, LV_OBJ_FLAG_HIDDEN));
  snapshot("editor-cache-pan.ppm");
  press(200, 280, false);
  for (int i = 0; i < 60; i++)
    step();
  assert(!lv_obj_has_flag(cache_scene, LV_OBJ_FLAG_HIDDEN));
  lv_obj_scroll_to_x(cache_canvas, 0, LV_ANIM_OFF);
  step();
  click_text("1.0s");
  tap(600, 280);
  click_text("保存");
  assert(projects_load(&l) == 1 && model_count(l.items[0].nodes, -1) == 20);
  puts("PASS: cached/live pixel identity, actual horizontal pan, scroll "
       "extent, restored parameter interaction and save");
  /* Tile windows must survive arbitrary jumps and retain clean tiles when a
   * single parameter changes. A 100-block scene exceeds the cache budget. */
  projects_empty(&l);
  l.items[0].used = 1;
  strcpy(l.items[0].name, "tile stress");
  for (int i = 0; i < 100; i++)
    model_add(l.items[0].nodes, WAIT, -1, i);
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  cache_scene = lv_obj_get_parent(
      lv_obj_get_parent(find_text(lv_screen_active(), "1.0s")));
  cache_canvas = lv_obj_get_parent(cache_scene);
  for (int pos = 0; pos <= 6800; pos += 850) {
    lv_obj_scroll_to_x(cache_canvas, pos, LV_ANIM_OFF);
    for (int i = 0; i < 20; i++)
      step();
    step();
    memcpy(before_cache, pixels, sizeof pixels);
    lv_obj_send_event(cache_canvas, LV_EVENT_SCROLL_BEGIN, NULL);
    step();
    assert(!memcmp(before_cache, pixels, sizeof pixels));
    EditorCacheStats st;
    editor_cache_stats(&st);
    assert(st.active && st.bytes <= st.budget && st.tiles <= 40);
    lv_obj_send_event(cache_canvas, LV_EVENT_SCROLL_END, NULL);
    step();
    assert(!memcmp(before_cache, pixels, sizeof pixels));
  }
  lv_obj_scroll_to_x(cache_canvas, 0, LV_ANIM_OFF);
  for (int i = 0; i < 20; i++)
    step();
  EditorCacheStats original, tile_changed;
  editor_cache_stats(&original);
  click_text("1.0s");
  step();
  editor_cache_stats(&tile_changed);
  assert(tile_changed.invalidated == original.invalidated);
  click_text("+");
  tap(600, 280);
  for (int i = 0; i < 20; i++)
    step();
  editor_cache_stats(&tile_changed);
  assert(tile_changed.invalidated > original.invalidated);
  assert(tile_changed.invalidated - original.invalidated < original.tiles);
  memcpy(before_cache, pixels, sizeof pixels);
  lv_obj_send_event(cache_canvas, LV_EVENT_SCROLL_BEGIN, NULL);
  step();
  assert(!memcmp(before_cache, pixels, sizeof pixels));
  lv_obj_send_event(cache_canvas, LV_EVENT_SCROLL_END, NULL);
  step();
  /* Long press selection remains separate from cached pixels. */
  press(100, 100, true);
  for (int i = 0; i < 10; i++)
    step();
  press(350, 235, true);
  press(350, 235, false);
  for (int i = 0; i < 12; i++)
    step();
  memcpy(before_cache, pixels, sizeof pixels);
  lv_obj_send_event(cache_canvas, LV_EVENT_SCROLL_BEGIN, NULL);
  step();
  assert(!memcmp(before_cache, pixels, sizeof pixels));
  lv_obj_send_event(cache_canvas, LV_EVENT_SCROLL_END, NULL);
  step();
  assert(!memcmp(before_cache, pixels, sizeof pixels));
  puts("PASS: 100-block bounded tile eviction, seam pixel identity, regional "
       "parameter damage and independent selection");
  editor_cache_stats(&original);
  lv_obj_scroll_to_x(cache_canvas, 6000, LV_ANIM_OFF);
  lv_refr_now(NULL);
  memcpy(before_cache, pixels, sizeof pixels);
  lv_obj_send_event(cache_canvas, LV_EVENT_SCROLL_BEGIN, NULL);
  lv_refr_now(NULL);
  editor_cache_stats(&tile_changed);
  assert(tile_changed.misses > original.misses);
  assert(!memcmp(before_cache, pixels, sizeof pixels));
  lv_obj_send_event(cache_canvas, LV_EVENT_SCROLL_END, NULL);
  step();
  puts("PASS: unprepared far jump falls back to live drawing without blank "
       "tiles");
  lv_obj_scroll_to(cache_canvas, 0, 0, LV_ANIM_OFF);
  step();
  press_palette_motor_stop();
  press(400, 330, true);
  for (int i = 0; i < 15; i++)
    step();
  assert(lv_obj_get_scroll_y(cache_canvas) == 0);
  press(400, 20, false);
  step();
  /* Deliberately dwelling at the right edge still scrolls a long scene. */
  press_palette_motor_stop();
  press(700, 240, true);
  press(790, 240, true);
  for (int i = 0; i < 15; i++)
    step();
  assert(lv_obj_get_scroll_x(cache_canvas) > 0);
  assert(lv_obj_get_scroll_y(cache_canvas) == 0);
  press(790, 20, false);
  step();
  projects_empty(&l);
  l.items[0].used = 1;
  strcpy(l.items[0].name, "short scene");
  model_add(l.items[0].nodes, WAIT, -1, 0);
  assert(projects_save(&l));
  editor_create();
  step();
  tap(220, 175);
  cache_scene = lv_obj_get_parent(
      lv_obj_get_parent(find_text(lv_screen_active(), "1.0s")));
  cache_canvas = lv_obj_get_parent(cache_scene);
  press_palette_motor_stop();
  press(400, 240, true);
  press(400, 330, true);
  for (int i = 0; i < 15; i++)
    step();
  assert(lv_obj_get_scroll_y(cache_canvas) == 0);
  assert(lv_obj_get_scroll_x(cache_canvas) == 0);
  press(400, 20, false);
  step();
  puts("PASS: palette entry does not lift scene, short scene cannot "
       "overscroll, deliberate horizontal edge scroll retained");
  projects_empty(&l);
  for (int i = 0; i < PROJECTS; i++) {
    l.items[i].used = 1;
    snprintf(l.items[i].name, 64, "Existing %d", i + 1);
    model_add(l.items[i].nodes, WAIT, -1, 0);
  }
  assert(projects_save(&l));
  editor_create();
  step();
  click_text("新建作品");
  assert(has_text(lv_screen_active(), "最多6个作品，请先删除一个"));
  WorkCatalog catalog = {0};
  assert(works_catalog(&catalog) && catalog.count == PROJECTS);
  click_text("...");
  click_text("删除");
  click_text("删除");
  assert(works_catalog(&catalog) && catalog.count == PROJECTS - 1);
  click_text("新建作品");
  click_text("电机正转");
  click_text("主页");
  assert(works_catalog(&catalog) && catalog.count == WORK_LIMIT);
  works_catalog_free(&catalog);
  puts("PASS: original six-work limit, deletion and slot reuse");
  Library invalid = l;
  invalid.version = 999;
  assert(!projects_save(&invalid));
  FILE *broken = fopen(testfile, "wb");
  assert(broken);
  fwrite("bad", 1, 3, broken);
  fclose(broken);
  assert((projects_load)(&invalid) == -1);
  assert(projects_valid(&invalid));
  test_clear_work_files();
  assert(DeleteFileA(testfile));
  for (int i = 0; i < PROJECTS; i++) {
    char f[MAX_PATH];
    snprintf(f, sizeof f, "%s.thumb-%d.bin", testfile, i);
    DeleteFileA(f);
  }
  /* Sharing survives deletion and cache pressure without losing live pixels. */
  lv_obj_t *holder = lv_obj_create(lv_screen_active());
  lv_obj_t *shape = block_shape(holder, 76, 36, false, false, 0x0090f5);
  const lv_image_dsc_t *shared = lv_image_get_src(shape);
  lv_obj_t *copy = block_shape(holder, 76, 36, false, false, 0x0090f5);
  assert(lv_image_get_src(copy) == shared);
  lv_obj_delete(shape);
  uint32_t saved;
  memcpy(&saved, shared->data + 1000, sizeof saved);
  for (int i = 0; i < 80; i++) {
    lv_obj_t *other = block_shape(holder, 100 + i, 50, true, false, 0xffb515);
    lv_obj_delete(other);
  }
  uint32_t current;
  memcpy(&current, shared->data + 1000, sizeof current);
  assert(current == saved);
  shape = block_shape(holder, 76, 36, false, false, 0x0090f5);
  assert(lv_image_get_src(shape) == shared);
  lv_obj_delete(holder);
  puts("PASS: 22 types, nesting/cycle/depth/capacity, stable group moves, "
       "legacy migration, touch decimal keypad, undo/redo, loop drag, matrix "
       "painting, staging, persistence, corrupted data rejection");
  return 0;
}
