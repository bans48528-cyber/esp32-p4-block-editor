#include "editor.h"
#include "block_shape.h"
#include "projects.h"
#include <assert.h>
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
  lv_obj_t *o=find_text(lv_screen_active(),text); assert(o);
  lv_area_t r;lv_obj_get_coords(o,&r);int x=(r.x1+r.x2)/2;
  press(x,280,true);for(int i=0;i<10;i++)step();
  press(x+1,(r.y1+r.y2)/2,true);press(x+1,(r.y1+r.y2)/2,false);
}
static void pull_staged(void) {
  press(90,400,true);press(90,360,true);press(450,160,true);press(450,160,false);
}
static void click_text(const char *text) {
  step();
  lv_obj_t *o = find_text(lv_screen_active(), text);
  if (!o) {
    const char *actions[]={"主页","撤销","前进","保存","清空"};
    for(int i=0;i<5;i++) if(!strcmp(text,actions[i])) {tap(i==0?32:544+56*i,27);return;}
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
  assert(migrated.version == 2 && migrated.items[0].nodes[0].value == 30);
  old.p[0].n[0].parent = 0;
  assert(!projects_decode(&migrated, &old, sizeof old));
  assert(projects_valid(&migrated));
}
int main(void) {
  model_tests();
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
  click_text("新建作品");
  snapshot("editor-empty.ppm");
  assert(!has_text(lv_screen_active(),"多选"));
  assert(!has_text(lv_screen_active(),"删除"));
  click_text("电机正转");
  assert(has_text(lv_screen_active(), "A 1.0s"));
  click_text("A 1.0s");
  assert(!has_text(lv_screen_active(), "退格"));
  click_text("+");
  assert(has_text(lv_screen_active(), "1.1"));
  click_text("-");
  snapshot("editor-parameter-popover.ppm");
  lv_area_t bubble_r, option_r;
  lv_obj_get_coords(find_text(lv_screen_active(), "A 1.0s"), &bubble_r);
  lv_obj_get_coords(find_text(lv_screen_active(), "A"), &option_r);
  assert(option_r.y1 > bubble_r.y2);

  click_text("1.0");
  snapshot("editor-keypad.ppm");
  click_text("2");
  click_text(".");
  click_text("5");
  click_text("B");
  click_text("确定");
  assert(has_text(lv_screen_active(), "B 2.5s"));
  click_text("撤销");
  assert(has_text(lv_screen_active(), "A 1.0s"));
  click_text("前进");
  assert(has_text(lv_screen_active(), "B 2.5s"));
  click_text("逻辑");
  click_text("次数");
  snapshot("editor-loop.ppm");
  /* Native pointer drag from the wait palette into the empty loop cavity. */
  press(60, 400, true);
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
  select_label_gesture("B 2.5s");
  select_label_gesture("绘制");
  press(130,166,true);press(530,390,true);
  for(int i=0;i<22;i++)step();
  press(530,390,false);
  assert(has_text(lv_screen_active(), "组合 1 · 2 张"));
  snapshot("editor-staged.ppm");
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(model_count(l.items[0].nodes, -2) == 2);
  /* Tapping or horizontal browsing never takes a group out of staging. */
  click_text("组合 1 · 2 张");
  drag(90,400,150,400);
  click_text("保存");assert(projects_load(&l)==1);
  assert(model_count(l.items[0].nodes,-2)==2);
  pull_staged();
  click_text("保存");
  assert(projects_load(&l) == 1);
  assert(model_count(l.items[0].nodes, -2) == 0);
  click_text("清空");
  tap(530, 288);
  assert(has_text(lv_screen_active(), "从下方拖入积木开始编程"));
  click_text("撤销");
  assert(has_text(lv_screen_active(), "B 2.5s"));
  click_text("主页");
  editor_create();
  step();
  tap(220, 175);
  assert(has_text(lv_screen_active(), "B 2.5s"));
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
    tap(650,280);
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
  click_text("A 999.9s");
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
  press(105,100,true);for(int i=0;i<10;i++)step();
  press(235,185,true);press(235,185,false);
  tap(650,280);
  press(125,155,true);press(500,390,true);
  for(int i=0;i<22;i++)step();
  press(500,390,false);
  click_text("保存");assert(projects_load(&l)==1);
  assert(model_count(l.items[0].nodes,-1)==4);
  assert(model_count(l.items[0].nodes,-2)==0);
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
  click_text("D 1.0s");
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
  assert(has_text(lv_screen_active(), "D 1.0s"));
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
  press(100,100,true);for(int i=0;i<10;i++)step();press(235,185,true);press(235,185,false);
  press(130,160,true);press(400,180,true);snapshot("editor-group-projection.ppm");
  unsigned group_a=pixels[145*800+102]&0xffffff,group_b=pixels[145*800+182]&0xffffff;
  assert(group_a!=0xeeeeee&&group_a!=0x0090f5);assert(group_b==group_a);
  press(400,30,true);press(400,30,false);
  /* A taller loop must keep the exposed right edge above its next block. */
  click_text("主页");assert(projects_load(&l)==1);memset(l.items[0].nodes,0,sizeof l.items[0].nodes);
  model_add(l.items[0].nodes,FOREVER,-1,0);model_add(l.items[0].nodes,MOTOR_STOP,-1,1);
  model_add(l.items[0].nodes,FOREVER,-1,2);model_add(l.items[0].nodes,LEFT,-1,3);
  assert(projects_save(&l));editor_create();step();tap(220,175);snapshot("editor-loop-edge.ppm");
  unsigned exposed_edge=pixels[123*800+233]&0xffffff;
  assert(exposed_edge!=blocks[FOREVER].color && exposed_edge!=0xeeeeee);
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
  Library invalid = l;
  invalid.version = 999;
  assert(!projects_save(&invalid));
  FILE *broken = fopen(testfile, "wb");
  assert(broken);
  fwrite("bad", 1, 3, broken);
  fclose(broken);
  assert(projects_load(&invalid) == -1);
  assert(projects_valid(&invalid));
  assert(DeleteFileA(testfile));
  /* Sharing survives deletion and cache pressure without losing live pixels. */
  lv_obj_t *holder=lv_obj_create(lv_screen_active());
  lv_obj_t *shape=block_shape(holder,76,36,false,false,0x0090f5);
  const lv_image_dsc_t *shared=lv_image_get_src(shape);
  lv_obj_t *copy=block_shape(holder,76,36,false,false,0x0090f5);
  assert(lv_image_get_src(copy)==shared);
  lv_obj_delete(shape);
  uint32_t saved;memcpy(&saved,shared->data+1000,sizeof saved);
  for(int i=0;i<80;i++) {
    lv_obj_t *other=block_shape(holder,100+i,50,true,false,0xffb515);
    lv_obj_delete(other);
  }
  uint32_t current;memcpy(&current,shared->data+1000,sizeof current);
  assert(current==saved);
  shape=block_shape(holder,76,36,false,false,0x0090f5);
  assert(lv_image_get_src(shape)==shared);
  lv_obj_delete(holder);
  puts("PASS: 17 types, nesting/cycle/depth/capacity, stable group moves, "
       "legacy migration, touch decimal keypad, undo/redo, loop drag, matrix "
       "painting, staging, persistence, corrupted data rejection");
  return 0;
}
