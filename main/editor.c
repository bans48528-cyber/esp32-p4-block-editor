#include "editor.h"
#include "block_shape.h"
#include "projects.h"
#include "toolbar_icons.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
LV_FONT_DECLARE(board_font);
static Node nodes[LIMIT], history[25][LIMIT];
static int history_pos, history_end;
static Library library;
static bool storage_ready;
static int active_project = -1, menu_project;
static lv_obj_t *name_input;
static void home(void), open_editor(void), back_home(lv_event_t *),
    save_work(lv_event_t *);
static lv_obj_t *canvas, *content, *status, *ghost, *modal, *palette, *tabs,
    *marker, *selection_box, *drop_zone;
static lv_obj_t *cards[LIMIT], *cavities[LIMIT], *undo_btn, *redo_btn,
    *bubbles[LIMIT];
static bool selected_nodes[LIMIT], multi;
static int category, source, kind, drag_group = -1, drop_parent, drop_pos;
static bool dragging, stage_hot;
static uint32_t hover_since;
static int staging_return = -1, staged_scroll;
static bool staged_panning;
static lv_point_t pressed;
static void render(void), render_palette(void), parameters(int);
static void say(const char *text) {
  if (status && strcmp(lv_label_get_text(status), text) != 0)
    lv_label_set_text(status, text);
}
static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, uint32_t color) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 5, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}
static lv_obj_t *label(lv_obj_t *p, const char *t) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, t);
  lv_obj_center(l);
  return l;
}
static void place_label(lv_obj_t *o, int x, int y) {
  lv_obj_set_align(o, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(o, x, y);
}
static lv_obj_t *button(lv_obj_t *p, int x, int y, int w, int h, const char *t,
                        lv_event_cb_t cb, int data) {
  lv_obj_t *b = box(p, x, y, w, h, 0xffffff);
  label(b, t);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  if (cb)
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)data);
  return b;
}
static lv_obj_t *decoration(lv_obj_t *p, int x, int y, int w, int h,
                            uint32_t c) {
  lv_obj_t *o = box(p, x, y, w, h, c);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}
static int count(int p) { return model_count(nodes, p); }
static int nth(int p, int o) { return model_nth(nodes, p, o); }
static bool contains(lv_obj_t *o, lv_point_t p) {
  if (!o)
    return false;
  lv_area_t r;
  lv_obj_get_coords(o, &r);
  return p.x >= r.x1 && p.x <= r.x2 && p.y >= r.y1 && p.y <= r.y2;
}
static void close_modal(lv_event_t *e) {
  (void)e;
  if (modal) {
    lv_obj_delete(modal);
    modal = NULL;
  }
}
static void render_later(void *p) {
  (void)p;
  render();
  render_palette();
}
static void schedule_render(void) { lv_async_call(render_later, NULL); }
static void history_reset(void) {
  history_pos = history_end = 0;
  memcpy(history[0], nodes, sizeof nodes);
  memset(selected_nodes, 0, sizeof selected_nodes);
  multi = false;
}
static void commit(void) {
  if (memcmp(history[history_pos], nodes, sizeof nodes) == 0)
    return;
  if (history_pos == 24) {
    memmove(history, history + 1, 24 * sizeof nodes);
    history_pos--;
  }
  memcpy(history[++history_pos], nodes, sizeof nodes);
  history_end = history_pos;
}
static void history_step(lv_event_t *e) {
  int p = history_pos + (int)(intptr_t)lv_event_get_user_data(e);
  if (p < 0 || p > history_end)
    return;
  history_pos = p;
  memcpy(nodes, history[p], sizeof nodes);
  memset(selected_nodes, 0, sizeof selected_nodes);
  schedule_render();
  say("已恢复编辑步骤");
}
static int free_group(void) {
  for (int g = 0; g < GROUPS; g++)
    if (!count(-2 - g))
      return g;
  return -1;
}
static void stage_selection(lv_event_t *e) {
  (void)e;
  int g = free_group();
  if (g < 0) {
    say("暂存区已满，请先取回一组");
    return;
  }
  if (!model_move(nodes, selected_nodes, -2 - g, 0)) {
    say("先选择需要暂存的积木");
    return;
  }
  memset(selected_nodes, 0, sizeof selected_nodes);
  commit();
  category = 5;
  schedule_render();
  say("已暂存，向上拖回工作区");
}
static const char *option_text(int t, int o) {
  static const char *compare[] = {"<", ">", "="}, *states[] = {"按下", "松开"},
                    *keys[] = {"左键", "右键"},
                    *notes[] = {"1", "2", "3", "4", "5"};
  if (t == ULTRASONIC || t == GRAYSCALE)
    return compare[o];
  if (t == BUTTON_SENSOR)
    return states[o];
  if (t == HOST_BUTTON)
    return keys[o];
  return notes[o];
}
static void number_text(char *out, size_t size, Node n) {
  if (blocks[n.type].decimal)
    snprintf(out, size, "%d.%d", (int)n.value / 10, (int)n.value % 10);
  else
    snprintf(out, size, "%d", (int)n.value);
}
static void param_text(char *out, size_t size, Node n) {
  char num[16];
  number_text(num, sizeof num, n);
  if (blocks[n.type].port) {
    if (blocks[n.type].max)
      snprintf(out, size, "%c %ss", 'A' + (int)n.port, num);
    else
      snprintf(out, size, "端口 %c", 'A' + (int)n.port);
  } else if (n.type == ULTRASONIC)
    snprintf(out, size, "%s %dcm", option_text(n.type, n.option), (int)n.value);
  else if (n.type == GRAYSCALE)
    snprintf(out, size, "%s %d", option_text(n.type, n.option), (int)n.value);
  else if (blocks[n.type].choices)
    snprintf(out, size, "%s", option_text(n.type, n.option));
  else if (n.type == FOREVER)
    snprintf(out, size, "持续");
  else if (n.type == MATRIX)
    snprintf(out, size, "绘制");
  else if (n.type == LOOP)
    snprintf(out, size, "%s次", num);
  else if (blocks[n.type].max)
    snprintf(out, size, "%ss", num);
  else
    snprintf(out, size, "停止");
}
/* Native icon primitives keep the board independent of SVG/browser runtimes. */
extern const lv_image_dsc_t *block_icons[];
static void icon(lv_obj_t *p, Node n, int x, int y) {
  if (n.type == MATRIX) {
    lv_obj_t *o = decoration(p, x, y, 40, 40, 0x571cc1);
    for (int i = 0; i < 25; i++)
      decoration(o, (i % 5) * 8 + 1, (i / 5) * 8 + 1, 6, 6,
                 (n.matrix & (1u << i)) ? 0xffffff : 0x7134cf);
    return;
  }
  lv_obj_t *o = lv_image_create(p);
  lv_image_set_src(o, block_icons[n.type]);
  lv_obj_set_pos(o, x, y);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
}
static Node draft;
static int editing;
static char number[16];
static bool fresh_number;
static lv_obj_t *param_panel, *number_pad;
static void quick_apply(void) {
  if (!number_pad) {
    nodes[editing] = draft;
    commit();
    schedule_render();
  }
}
static lv_obj_t *numeric, *param_error, *choice_btn[5], *port_btn[4],
    *matrix_grid, *matrix_cells[25];
static bool painting, paint_on;
static int last_cell = -1;
static void update_choices(void) {
  for (int group = 0; group < 2; group++)
    for (int i = 0; i < (group ? 5 : 4); i++) {
      lv_obj_t *o = group ? choice_btn[i] : port_btn[i];
      if (!o)
        continue;
      bool selected = (group ? draft.option : draft.port) == i;
      lv_obj_set_style_bg_color(
          o, lv_color_hex(selected ? blocks[draft.type].color : 0xf4f7f9), 0);
      lv_obj_set_style_text_color(
          o, lv_color_hex(selected ? 0xffffff : 0x405366), 0);
      lv_obj_set_style_border_width(o, selected ? 2 : 1, 0);
      lv_obj_set_style_border_color(
          o, lv_color_hex(selected ? 0xffffff : 0xdce3e9), 0);
    }
}
static void choose(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= 10)
    draft.port = i - 10;
  else
    draft.option = i;
  update_choices();
  quick_apply();
}
static void keypad(lv_event_t *e) {
  int key = (int)(intptr_t)lv_event_get_user_data(e);
  size_t len = strlen(number);
  if (key == 11) {
    if (len)
      number[len - 1] = 0;
    fresh_number = false;
  } else if (key == 12) {
    number[0] = 0;
    fresh_number = false;
  } else {
    if (fresh_number) {
      number[0] = 0;
      len = 0;
      fresh_number = false;
    }
    if (len < 7) {
      if (key == 10) {
        if (blocks[draft.type].decimal && !strchr(number, '.')) {
          if (!len)
            strcpy(number, "0");
          strcat(number, ".");
        }
      } else {
        char *dot = strchr(number, '.');
        if (!dot || strlen(dot) < 2) {
          len = strlen(number);
          number[len] = (char)('0' + key);
          number[len + 1] = 0;
        }
      }
    }
  }
  lv_label_set_text(numeric, number[0] ? number : "0");
  lv_label_set_text(param_error, "");
}
static void matrix_refresh(void) {
  for (int i = 0; i < 25; i++)
    lv_obj_set_style_bg_color(
        matrix_cells[i],
        lv_color_hex((draft.matrix & (1u << i)) ? 0xffdf5b : 0x3b526d), 0);
}
static void paint_cell(int i) {
  if (i < 0 || i >= 25)
    return;
  if (paint_on)
    draft.matrix |= 1u << i;
  else
    draft.matrix &= ~(1u << i);
}
static void paint_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *in = lv_indev_active();
  if (!in)
    return;
  lv_point_t p;
  lv_indev_get_point(in, &p);
  lv_area_t r;
  lv_obj_get_coords(matrix_grid, &r);
  int x = (int)(p.x - r.x1) / 48, y = (int)(p.y - r.y1) / 48, i = x + y * 5;
  if (p.x < r.x1 || p.y < r.y1 || x > 4 || y > 4)
    i = -1;
  if (code == LV_EVENT_PRESSED && i >= 0) {
    painting = true;
    last_cell = i;
    paint_on = !(draft.matrix & (1u << i));
    paint_cell(i);
    matrix_refresh();
  }
  if (code == LV_EVENT_PRESSING && painting && i >= 0) {
    if (last_cell >= 0) {
      int ax = last_cell % 5, ay = last_cell / 5, dx = x - ax, dy = y - ay,
          steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
      for (int k = 1; k <= steps; k++)
        paint_cell(ax + dx * k / steps + 5 * (ay + dy * k / steps));
    }
    paint_cell(i);
    last_cell = i;
    matrix_refresh();
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    painting = false;
    last_cell = -1;
  }
}
static void matrix_preset(lv_event_t *e) {
  int k = (int)(intptr_t)lv_event_get_user_data(e);
  draft.matrix = k == 0   ? 0
                 : k == 1 ? 0x1ffffffu
                 : k == 2 ? 0x1151151u
                          : 0x0e8c62eu;
  matrix_refresh();
}
static void commit_param(lv_event_t *e) {
  (void)e;
  const BlockDef *b = &blocks[draft.type];
  lv_obj_set_style_text_color(param_error, lv_color_hex(0xce3c50), 0);
  if (b->max) {
    if (!number[0] || strcmp(number, ".") == 0) {
      lv_label_set_text(param_error, "请输入数值");
      return;
    }
    char *end;
    double v = strtod(number, &end);
    int value = (int)(v * (b->decimal ? 10 : 1) + 0.5);
    if (*end || value < b->min || value > b->max) {
      lv_label_set_text(param_error, "数值超出范围");
      return;
    }
    draft.value = value;
  }
  nodes[editing] = draft;
  commit();
  close_modal(NULL);
  schedule_render();
  say("参数已修改");
}
static void hide_number_pad(lv_event_t *e) {
  (void)e;
  if (number_pad) {
    lv_obj_delete(number_pad);
    number_pad = NULL;
  }
}
static void show_number_pad(lv_event_t *e) {
  (void)e;
  if (number_pad)
    return;
  fresh_number = true;
  lv_obj_update_layout(param_panel);
  int x = lv_obj_get_x(param_panel) + lv_obj_get_width(param_panel) + 8,
      y = lv_obj_get_y(param_panel);
  if (x + 204 > 792)
    x = lv_obj_get_x(param_panel) - 212;
  if (x < 8)
    x = 588;
  if (y > 158)
    y = 158;
  if (y < 8)
    y = 8;
  number_pad = box(modal, x, y, 204, 314, blocks[draft.type].color);
  lv_obj_set_style_border_width(number_pad, 1, 0);
  lv_obj_set_style_border_color(number_pad, lv_color_hex(0x9bb5d3), 0);
  const char *keys[] = {"1", "2", "3", "4", "5", "6",
                        "7", "8", "9", ".", "0", "退格"};
  int values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11};
  for (int k = 0; k < 12; k++) {
    lv_obj_t *key = button(number_pad, 8 + (k % 3) * 64, 8 + (k / 3) * 50, 60,
                           46, keys[k], keypad, values[k]);
    if (k == 9 && !blocks[draft.type].decimal)
      lv_obj_add_state(key, LV_STATE_DISABLED);
  }
  button(number_pad, 8, 212, 90, 44, "清除", keypad, 12);
  button(number_pad, 106, 212, 90, 44, "收起", hide_number_pad, 0);
  button(number_pad, 8, 262, 90, 44, "取消", close_modal, 0);
  button(number_pad, 106, 262, 90, 44, "确定", commit_param, 0);
}
static void step_number(lv_event_t *e) {
  const BlockDef *b = &blocks[draft.type];
  char *end;
  double v = strtod(number, &end);
  int value =
      number[0] && !*end ? (int)(v * (b->decimal ? 10 : 1) + .5) : draft.value;
  value += (int)(intptr_t)lv_event_get_user_data(e);
  if (value < b->min)
    value = b->min;
  if (value > b->max)
    value = b->max;
  Node preview = draft;
  preview.value = value;
  if (!number_pad) {
    draft.value = value;
    quick_apply();
  }
  number_text(number, sizeof number, preview);
  lv_label_set_text(numeric, number);
  lv_label_set_text(param_error, "");
  fresh_number = true;
}
static void backdrop_close(lv_event_t *e) {
  if (lv_event_get_target(e) == modal)
    close_modal(NULL);
}
static void parameters(int id) {
  if (modal || id < 0 || !nodes[id].type)
    return;
  const BlockDef *b = &blocks[nodes[id].type];
  if (!b->max && !b->port && !b->choices && id >= 0 &&
      nodes[id].type != MATRIX) {
    say("此积木没有可调参数");
    return;
  }
  editing = id;
  draft = nodes[id];
  fresh_number = true;
  memset(choice_btn, 0, sizeof choice_btn);
  memset(port_btn, 0, sizeof port_btn);
  modal = box(lv_screen_active(), 0, 0, 800, 480, 0xeaf0f8);
  lv_obj_t *heading = label(modal, b->name);
  place_label(heading, 28, 18);
  button(modal, 580, 414, 92, 52, "取消", close_modal, 0);
  button(modal, 688, 414, 92, 52, "确定", commit_param, 0);
  param_error = label(modal, "");
  place_label(param_error, 30, 425);
  lv_obj_set_style_text_color(param_error, lv_color_hex(0xce3c50), 0);
  if (draft.type == MATRIX) {
    matrix_grid = box(modal, 180, 100, 240, 240, 0x273952);
    lv_obj_add_flag(matrix_grid, LV_OBJ_FLAG_PRESS_LOCK);
    for (int i = 0; i < 25; i++)
      matrix_cells[i] = decoration(matrix_grid, (i % 5) * 48 + 3,
                                   (i / 5) * 48 + 3, 42, 42, 0x273952);
    lv_obj_add_event_cb(matrix_grid, paint_event, LV_EVENT_ALL, NULL);
    matrix_refresh();
    lv_obj_t *hint = label(modal, "按住划过可连续绘制或擦除");
    place_label(hint, 180, 65);
    button(modal, 475, 110, 140, 52, "清空点阵", matrix_preset, 0);
    button(modal, 630, 110, 140, 52, "全部点亮", matrix_preset, 1);
    button(modal, 475, 182, 140, 52, "图案一", matrix_preset, 2);
    button(modal, 630, 182, 140, 52, "图案二", matrix_preset, 3);
    return;
  }
  /* The reference popover is centered directly beneath the block. */
  lv_obj_clean(modal);
  number_pad = NULL;
  lv_obj_set_style_bg_opa(modal, LV_OPA_TRANSP, 0);
  lv_obj_add_event_cb(modal, backdrop_close, LV_EVENT_CLICKED, NULL);
  lv_area_t anchor;
  lv_obj_get_coords(bubbles[id] ? bubbles[id] : cards[id], &anchor);
  bool options = b->port || b->choices;
  int width = b->choices == 5 ? 260 : 232;
  int height = options && b->max ? 144 : 84;
  int cx = (anchor.x1 + anchor.x2) / 2, x = cx - width / 2, y = anchor.y2 + 14;
  if (x < 8)
    x = 8;
  if (x + width > 792)
    x = 792 - width;
  bool above = false;
  if (y + height > 472) {
    above = true;
    y = anchor.y1 - height - 14;
  }
  if (y < 8)
    y = 8;
  /* A small raster-independent triangular pointer connects the panel to its
   * block. */
  int tip = cx - x;
  if (tip < 14)
    tip = 14;
  if (tip > width - 14)
    tip = width - 14;
  for (int i = 0; i < 9; i++) {
    lv_obj_t *row =
        decoration(modal, x + tip - i, above ? y + height + 8 - i : y - 8 + i,
                   i * 2 + 1, 1, b->color);
    lv_obj_set_style_radius(row, 0, 0);
  }
  param_panel = box(modal, x, y, width, height, b->color);
  lv_obj_set_style_radius(param_panel, 12, 0);
  lv_obj_set_style_border_width(param_panel, 1, 0);
  lv_obj_set_style_border_color(param_panel, lv_color_hex(0x0078bb), 0);
  lv_obj_set_style_shadow_width(param_panel, 16, 0);
  lv_obj_set_style_shadow_opa(param_panel, LV_OPA_20, 0);
  lv_obj_set_style_shadow_ofs_y(param_panel, 5, 0);
  if (b->port) {
    for (int i = 0; i < 4; i++) {
      char text[2] = {(char)('A' + i), 0};
      port_btn[i] =
          button(param_panel, 19 + i * 50, 15, 44, 44, text, choose, 10 + i);
    }
  } else if (b->choices) {
    int w = b->choices == 5 ? 44 : 60, gap = b->choices == 5 ? 5 : 8;
    int left = (width - (b->choices * w + (b->choices - 1) * gap)) / 2;
    for (int i = 0; i < b->choices; i++) {
      choice_btn[i] = button(param_panel, left + i * (w + gap), 15, w, 44,
                             option_text(draft.type, i), choose, i);
    }
  }
  if (b->max) {
    int row = options ? 84 : 16;
    if (options) {
      lv_obj_t *divider =
          decoration(param_panel, 14, 71, width - 28, 1, 0xffffff);
      lv_obj_set_style_bg_opa(divider, LV_OPA_30, 0);
    }
    lv_obj_t *minus =
        button(param_panel, 15, row, 44, 44, "-", step_number, -1);
    lv_obj_t *plus =
        button(param_panel, width - 59, row, 44, 44, "+", step_number, 1);
    lv_obj_set_style_radius(minus, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(plus, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_color(minus, lv_color_hex(0x0078bb), 0);
    lv_obj_set_style_text_color(plus, lv_color_hex(0x0078bb), 0);
    number_text(number, sizeof number, draft);
    lv_obj_t *field = button(param_panel, 69, row, width - 138, 44, number,
                             show_number_pad, 0);
    numeric = lv_obj_get_child(field, 0);
    lv_obj_set_style_bg_color(field, lv_color_hex(0xf4f7f9), 0);
    if (b->decimal || draft.type == LOOP || draft.type == ULTRASONIC) {
      const char *unit = b->decimal ? "秒" : draft.type == LOOP ? "次" : "cm";
      lv_obj_t *u = label(field, unit);
      lv_obj_align(u, LV_ALIGN_RIGHT_MID, -7, 0);
      lv_obj_set_style_text_color(u, lv_color_hex(0x6f8090), 0);
      lv_obj_align(numeric, LV_ALIGN_CENTER, -12, 0);
    }
  }
  param_error = label(modal, "");
  place_label(param_error, x, y + height + 28 <= 480 ? y + height + 4 : y - 26);
  lv_obj_set_style_text_color(param_error, lv_color_hex(0xce3c50), 0);
  update_choices();
}
static void param_click(lv_event_t *e) {
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  parameters(id);
}
/* Matching connector baselines: nested loops grow up by 14 pixels per level. */
typedef struct {
  int width, anchor, height;
} Size;
static lv_obj_t *projection_layer, *projection_cards[LIMIT],
    *projection_cavities[LIMIT];
static bool projection_drawing, projected[LIMIT], projection_valid;
static int projection_parent, projection_pos;
static Node projection_model[LIMIT];
static Size sizes[LIMIT];
static lv_obj_t *selection_outline, *selection_background;
static void update_selection_outline(void) {
  if (projection_drawing)
    return;
  lv_obj_update_layout(content);
  lv_area_t bounds = {0};
  bool found = false;
  for (int i = 0; i < LIMIT; i++)
    if (cards[i]) {
      bool selected = false;
      for (int j = 0; j < LIMIT; j++)
        if (selected_nodes[j] && model_descendant(nodes, i, j)) {
          selected = true;
          break;
        }
      if (!selected)
        continue;
      for (int part = 0; part < 2; part++) {
        lv_obj_t *obj = part ? bubbles[i] : cards[i];
        if (!obj)
          continue;
        lv_area_t r;
        lv_obj_get_coords(obj, &r);
        if (!part)
          r.y2 = r.y1 + sizes[i].anchor + 35;
        if (!found) {
          bounds = r;
          found = true;
        } else {
          if (r.x1 < bounds.x1)
            bounds.x1 = r.x1;
          if (r.y1 < bounds.y1)
            bounds.y1 = r.y1;
          if (r.x2 > bounds.x2)
            bounds.x2 = r.x2;
          if (r.y2 > bounds.y2)
            bounds.y2 = r.y2;
        }
      }
    }
  if (!found) {
    if (selection_outline) {
      lv_obj_delete(selection_outline);
      selection_outline = NULL;
    }
    if (selection_background) {
      lv_obj_delete(selection_background);
      selection_background = NULL;
    }
    return;
  }
  if (!selection_background) {
    selection_background = decoration(content, 0, 0, 1, 1, 0xbddcff);
    lv_obj_set_style_radius(selection_background, 7, 0);
    lv_obj_add_flag(selection_background, LV_OBJ_FLAG_IGNORE_LAYOUT);
    /* Behind every card and parameter, while remaining above the canvas. */
    lv_obj_move_to_index(selection_background, 0);
  }
  if (!selection_outline) {
    selection_outline = decoration(content, 0, 0, 1, 1, 0x1747cb);
    lv_obj_set_style_bg_opa(selection_outline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(selection_outline, lv_color_hex(0x1747cb), 0);
    lv_obj_set_style_border_width(selection_outline, 2, 0);
    lv_obj_set_style_radius(selection_outline, 7, 0);
    lv_obj_add_flag(selection_outline, LV_OBJ_FLAG_IGNORE_LAYOUT);
  }
  lv_area_t origin;
  lv_obj_get_coords(content, &origin);
  lv_obj_set_pos(selection_outline, bounds.x1 - origin.x1 - 3,
                 bounds.y1 - origin.y1 - 3);
  lv_obj_set_size(selection_outline, bounds.x2 - bounds.x1 + 7,
                  bounds.y2 - bounds.y1 + 7);
  lv_obj_set_pos(selection_background, bounds.x1 - origin.x1 - 3,
                 bounds.y1 - origin.y1 - 3);
  lv_obj_set_size(selection_background, bounds.x2 - bounds.x1 + 7,
                  bounds.y2 - bounds.y1 + 7);
}
static Size sequence_size(int p);
static Size measure(int id) {
  Size m = {76, 36, 108};
  if (is_loop(nodes[id].type)) {
    Size inner = sequence_size(id);
    m.width = 26 + inner.width + 60;
    m.anchor = 14 + inner.anchor;
    m.height = 14 + inner.height;
    if (m.height < m.anchor + 72)
      m.height = m.anchor + 72;
  }
  sizes[id] = m;
  return m;
}
static Size sequence_size(int p) {
  Size m = {0, 36, 108};
  int below = 72;
  for (int j = 0; j < count(p); j++) {
    Size s = measure(nth(p, j));
    m.width += s.width;
    if (s.anchor > m.anchor)
      m.anchor = s.anchor;
    if (s.height - s.anchor > below)
      below = s.height - s.anchor;
  }
  if (!m.width)
    m.width = 56;
  m.height = m.anchor + below;
  return m;
}
static void draggable(lv_obj_t *o, int data);
static void draw_node(lv_obj_t *p, int id, int x, int y) {
  Node n = nodes[id];
  Size m = sizes[id];
  uint32_t c = blocks[n.type].color;
  lv_obj_t *o = box(p, x, y, m.width + 8, m.height, c);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  cards[id] = o;
  draggable(o, id);
  int bx = 0, by = 0, bw = 76;
  block_shape(o, m.width, m.anchor, is_loop(n.type), false, c);
  lv_obj_add_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  if (is_loop(n.type)) {
    bx = m.width - 60;
    by = m.anchor - 36;
    bw = 60;
    Size inner = sequence_size(id);
    /* Paint bounds include the last child's plug; sequence advance stays
     * unchanged. */
    lv_obj_t *cavity = box(o, 26, 14, inner.width + 8, inner.height, 0xeeeeee);
    lv_obj_set_style_bg_opa(cavity, LV_OPA_TRANSP, 0);
    cavities[id] = cavity;
    lv_obj_add_flag(cavity, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_border_color(cavity, lv_color_hex(0x356bf2), 0);
    lv_obj_remove_flag(cavity, LV_OBJ_FLAG_CLICKABLE);
    if (!count(id) && !(projection_drawing && projected[id])) {
      lv_obj_t *hint = label(cavity, "+");
      lv_obj_set_style_text_color(hint, lv_color_hex(0x8995a7), 0);
    }
    int cx = 0;
    for (int j = 0; j < count(id); j++) {
      int child = nth(id, j);
      draw_node(cavity, child, cx, inner.anchor - sizes[child].anchor);
      cx += sizes[child].width;
    }
  }
  if (projection_drawing && projected[id]) {
    if (n.parent < 0 || !projected[n.parent])
      lv_obj_set_style_opa(o, LV_OPA_30, 0);
    return;
  }
  icon(o, n, bx + (bw - 40) / 2, by + 16);
  if (n.type == FOREVER)
    return;
  char text[32];
  param_text(text, sizeof text, n);
  lv_obj_t *b = button(o, bx + 2, by + 64, bw - 4, 44, text, param_click, id);
  bubbles[id] = b;
  lv_obj_set_style_radius(b, 24, 0);
  lv_obj_set_style_text_font(b, &board_font, 0);
  lv_point_t text_size;
  lv_text_get_size(&text_size, text, &board_font, 0, 0, 1000,
                   LV_TEXT_FLAG_NONE);
  int bubble_width = text_size.x + 12;
  if (bubble_width < bw - 8)
    bubble_width = bw - 8;
  lv_obj_set_width(b, bubble_width);
  lv_obj_set_x(b, bx + (bw - bubble_width) / 2);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(c), 0);
}
static bool excluded(int id) {
  if (source < 0 && drag_group < 0)
    return false;
  for (int i = 0; i < LIMIT; i++)
    if (selected_nodes[i] && model_descendant(nodes, id, i))
      return true;
  return false;
}
/* Preview the transaction in a separate, non-interactive layer. Keep the
 * pressed object's tree alive until release; the real model/history are never
 * changed. */
static void projection_clear(void) {
  if (projection_layer) {
    lv_obj_delete(projection_layer);
    projection_layer = NULL;
  }
  if (content)
    lv_obj_set_style_opa(content, LV_OPA_COVER, 0);
  memset(projection_cards, 0, sizeof projection_cards);
  memset(projection_cavities, 0, sizeof projection_cavities);
}
static void no_input(lv_obj_t *o) {
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
    no_input(lv_obj_get_child(o, i));
}
static bool project_at(int parent, int pos) {
  /* The source model and selection remain fixed until this drag ends. */
  if (projection_layer && projection_parent == parent && projection_pos == pos)
    return true;
  Node candidate[LIMIT];
  memcpy(candidate, nodes, sizeof candidate);
  memset(projected, 0, sizeof projected);
  bool moving = source >= 0 || drag_group >= 0;
  if (moving) {
    for (int i = 0; i < LIMIT; i++)
      if (candidate[i].type)
        for (int j = 0; j < LIMIT; j++)
          if (selected_nodes[j] && model_descendant(nodes, i, j))
            projected[i] = true;
    if (!model_move(candidate, selected_nodes, parent, pos))
      return false;
  } else {
    int id = model_add(candidate, kind, parent, pos);
    if (id < 0)
      return false;
    projected[id] = true;
  }
  if (projection_layer && projection_parent == parent &&
      projection_pos == pos &&
      memcmp(candidate, projection_model, sizeof candidate) == 0)
    return true;
  projection_clear();
  memcpy(projection_model, candidate, sizeof candidate);
  projection_parent = parent;
  projection_pos = pos;
  Node original[LIMIT];
  Size old_sizes[LIMIT];
  lv_obj_t *old_cards[LIMIT], *old_cavities[LIMIT], *old_bubbles[LIMIT],
      *original_content = content;
  memcpy(original, nodes, sizeof original);
  memcpy(old_sizes, sizes, sizeof sizes);
  memcpy(old_cards, cards, sizeof cards);
  memcpy(old_cavities, cavities, sizeof cavities);
  memcpy(old_bubbles, bubbles, sizeof bubbles);
  projection_layer = box(canvas, 0, 0, 800, 264, 0xeeeeee);
  content = projection_layer;
  memcpy(nodes, candidate, sizeof candidate);
  projection_drawing = true;
  render();
  projection_drawing = false;
  memcpy(projection_cards, cards, sizeof cards);
  memcpy(projection_cavities, cavities, sizeof cavities);
  memcpy(nodes, original, sizeof original);
  memcpy(sizes, old_sizes, sizeof sizes);
  memcpy(cards, old_cards, sizeof cards);
  memcpy(cavities, old_cavities, sizeof cavities);
  memcpy(bubbles, old_bubbles, sizeof bubbles);
  content = original_content;
  no_input(projection_layer);
  lv_obj_set_style_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_update_layout(canvas);
  return true;
}
static int target_parent(lv_point_t p) {
  int target = -1, depth = -1;
  for (int i = 0; i < LIMIT; i++) {
    lv_obj_t *cavity = projection_layer ? projection_cavities[i] : cavities[i];
    if (cavity && !excluded(i) && contains(cavity, p)) {
      int d = 0;
      for (int q = i; q >= 0; q = nodes[q].parent)
        d++;
      if (d > depth) {
        target = i;
        depth = d;
      }
    }
  }
  return target;
}
static void show_marker(lv_point_t p) {
  /* Do not rearrange the chain while the pointer is only crossing blank space
   * on its way up from the palette. Wait until it reaches the instruction band.
   */
  if (count(-1)) {
    int top = 480, bottom = 0;
    for (int j = 0; j < count(-1); j++) {
      int id = nth(-1, j);
      lv_obj_t *o = projection_layer ? projection_cards[id] : cards[id];
      if (!o)
        continue;
      lv_area_t r;
      lv_obj_get_coords(o, &r);
      if (r.y1 < top)
        top = r.y1;
      if (r.y2 > bottom)
        bottom = r.y2;
    }
    if (p.y < top || p.y > bottom) {
      projection_clear();
      projection_valid = false;
      return;
    }
  }

  /* Keep a target stable while the pointer is inside its projected volume. */
  if (projection_layer) {
    for (int i = 0; i < LIMIT; i++)
      if (projected[i] && contains(projection_cards[i], p)) {
        drop_parent = projection_parent;
        drop_pos = projection_pos;
        projection_valid = true;
        return;
      }
  }
  drop_parent = target_parent(p);
  drop_pos = count(drop_parent);
  for (int j = 0; j < count(drop_parent); j++) {
    int id = nth(drop_parent, j);
    if (excluded(id))
      continue;
    lv_obj_t *card = projection_layer ? projection_cards[id] : cards[id];
    if (!card)
      continue;
    lv_area_t r;
    lv_obj_get_coords(card, &r);
    if (p.x < (r.x1 + r.x2) / 2) {
      drop_pos = j;
      break;
    }
  }
  projection_valid = project_at(drop_parent, drop_pos);
  if (!projection_valid)
    projection_clear();
}
static void restore_staging_category(void) {
  if (staging_return >= 0) {
    category = staging_return;
    staging_return = -1;
    render_palette();
  }
}
static bool can_stage_drag(void) {
  if (source < 0)
    return false;
  int total = 0;
  for (int i = 0; i < LIMIT; i++)
    if (nodes[i].type)
      for (int j = 0; j < LIMIT; j++)
        if (selected_nodes[j] && model_descendant(nodes, i, j)) {
          total++;
          break;
        }
  return total > 1;
}
static void drag_cleanup(void) {
  restore_staging_category();
  projection_clear();
  if (drop_zone) {
    lv_obj_delete(drop_zone);
    drop_zone = NULL;
  }
  if (ghost) {
    lv_obj_delete(ghost);
    ghost = NULL;
  }
  if (marker) {
    lv_obj_delete(marker);
    marker = NULL;
  }
  for (int i = 0; i < LIMIT; i++)
    if (cavities[i])
      lv_obj_set_style_border_width(cavities[i], 0, 0);
  lv_obj_set_scroll_dir(canvas, LV_DIR_ALL);
}
static void drag_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  int data = (int)(intptr_t)lv_event_get_user_data(e);
  lv_indev_t *input = lv_indev_active();
  if (!input)
    return;
  lv_point_t p;
  lv_indev_get_point(input, &p);
  if (code == LV_EVENT_PRESSED) {
    pressed = p;
    source = data >= 0 ? data : -1;
    drag_group = data <= -100 ? -data - 100 : -1;
    kind = data >= 0 ? nodes[data].type : -data;
    dragging = false;
    staged_panning = false;
    staged_scroll = lv_obj_get_scroll_x(palette);
    staging_return = -1;
    hover_since = 0;
    stage_hot = false;
    projection_valid = false;
    return;
  }
  if (code == LV_EVENT_PRESSING) {
    if (drag_group >= 0 && !dragging) {
      int dx = p.x - pressed.x, dy = p.y - pressed.y;
      if (staged_panning || (abs(dx) > 6 && abs(dx) >= abs(dy))) {
        staged_panning = true;
        lv_obj_scroll_to_x(palette, staged_scroll - dx, LV_ANIM_OFF);
        return;
      }
      if (!(dy < -18 && abs(dy) * 10 > abs(dx) * 7))
        return;
    }
    if (!dragging && abs(p.x - pressed.x) + abs(p.y - pressed.y) > 12) {
      dragging = true;
      lv_obj_set_scroll_dir(canvas, LV_DIR_NONE);
      if (source >= 0 && !selected_nodes[source]) {
        memset(selected_nodes, 0, sizeof selected_nodes);
        selected_nodes[source] = true;
      }
      if (drag_group >= 0) {
        memset(selected_nodes, 0, sizeof selected_nodes);
        for (int i = 0; i < LIMIT; i++)
          if (nodes[i].type && nodes[i].parent == -2 - drag_group)
            selected_nodes[i] = true;
      }
      if (source >= 0) {
        for (int i = 0; i < LIMIT; i++)
          if (nodes[i].parent != nodes[source].parent)
            selected_nodes[i] = false;
      }
      int selected_count = 0;
      for (int i = 0; i < LIMIT; i++)
        if (selected_nodes[i])
          selected_count++;
      multi = selected_count > 1;
      Node floating = source >= 0 ? nodes[source] : node_default(kind);
      bool simple = drag_group < 0 && (!multi || source < 0);
      int width = simple ? (is_loop(kind) ? 142 : 76) : 100,
          anchor = simple && is_loop(kind) ? 50 : 36;
      ghost = box(lv_layer_top(), p.x - 40, p.y - 30, width + 8, anchor + 72,
                  0x356be8);
      lv_obj_remove_flag(ghost, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(ghost, LV_OPA_70, 0);
      lv_obj_set_style_text_font(ghost, &board_font, 0);
      if (simple) {
        lv_obj_set_style_bg_opa(ghost, LV_OPA_TRANSP, 0);
        block_shape(ghost, width, anchor, is_loop(kind), false,
                    blocks[kind].color);
        int tail = is_loop(kind) ? width - 60 : 0;
        icon(ghost, floating, tail + (is_loop(kind) ? 10 : 18), anchor - 20);
        if (floating.type != FOREVER) {
          char text[32];
          param_text(text, sizeof text, floating);
          lv_obj_t *tag = decoration(ghost, tail + 2, anchor + 28,
                                     is_loop(kind) ? 56 : 72, 44, 0xffffff);
          lv_obj_set_style_radius(tag, 24, 0);
          label(tag, text);
        }
      } else
        label(ghost, drag_group >= 0 ? "暂存组合" : "移动所选");
    }
    if (dragging) {
      lv_obj_set_pos(ghost, p.x - 40, p.y - 30);
      if (p.y >= 54 && p.y < 318) {
        restore_staging_category();
        if (drop_zone) {
          lv_obj_delete(drop_zone);
          drop_zone = NULL;
        }
        hover_since = 0;
        stage_hot = false;
        show_marker(p);
        if (p.x > 770)
          lv_obj_scroll_by(canvas, -10, 0, LV_ANIM_OFF);
        if (p.x < 24)
          lv_obj_scroll_by(canvas, 10, 0, LV_ANIM_OFF);
        if (p.y < 76)
          lv_obj_scroll_by(canvas, 0, 8, LV_ANIM_OFF);
        if (p.y > 294)
          lv_obj_scroll_by(canvas, 0, -8, LV_ANIM_OFF);
        say(projection_valid ? "松手放入半透明占位处" : "此处无法放入");
      } else if (p.y >= 318 && p.y < 454) {
        projection_clear();
        projection_valid = false;
        if (!drop_zone && (source >= 0 || drag_group >= 0)) {
          drop_zone = decoration(lv_layer_top(), 0, 318, 800, 136, 0xf3bac1);
          lv_obj_set_style_text_font(drop_zone, &board_font, 0);
          label(drop_zone, "松手删除");
        }
        if (marker) {
          lv_obj_delete(marker);
          marker = NULL;
        }
        if (source >= 0) {
          if (!hover_since)
            hover_since = lv_tick_get();
          stage_hot = can_stage_drag() &&
                      (category == 5 || lv_tick_elaps(hover_since) >= 1000);
          if (stage_hot && category != 5) {
            staging_return = category;
            category = 5;
            render_palette();
          }
          say(stage_hot          ? "松手暂存所选积木"
              : can_stage_drag() ? "松手删除；停留一秒改为暂存"
                                 : "松手删除");
          if (drop_zone) {
            lv_obj_set_style_bg_color(
                drop_zone, lv_color_hex(stage_hot ? 0xb6d5ff : 0xf3bac1), 0);
            const char *hint = stage_hot ? "松手暂存"
                               : can_stage_drag()
                                   ? "松手删除 · 停留一秒改为暂存"
                                   : "松手删除";
            lv_obj_t *text = lv_obj_get_child(drop_zone, 0);
            if (strcmp(lv_label_get_text(text), hint))
              lv_label_set_text(text, hint);
          }
        } else if (drag_group >= 0)
          say("拖回工作区，或在下方松手删除组");
      } else {
        restore_staging_category();
        hover_since = 0;
        stage_hot = false;
        projection_clear();
        projection_valid = false;
      }
    }
    return;
  }
  if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST)
    return;
  if (dragging && code == LV_EVENT_RELEASED && p.y >= 54 && p.y < 318)
    show_marker(p);
  drag_cleanup();
  if (code == LV_EVENT_PRESS_LOST) {
    dragging = false;
    return;
  }
  if (!dragging) {
    if (drag_group >= 0) {
      say("向上拖回工作区；左右滑动浏览暂存");
    } else if (source >= 0) {
      parameters(source);
    } else {
      if (model_add(nodes, kind, -1, count(-1)) >= 0) {
        commit();
        schedule_render();
        say("已添加积木");
      } else
        say("作品最多 32 张积木");
    }
    return;
  }
  dragging = false;
  if (p.y >= 318 && p.y < 454 && (source >= 0 || drag_group >= 0)) {
    if (source >= 0 && stage_hot) {
      stage_selection(NULL);
      return;
    }
    if (source >= 0 || drag_group >= 0) {
      model_delete(nodes, selected_nodes);
      memset(selected_nodes, 0, sizeof selected_nodes);
      commit();
      schedule_render();
      say("已删除，可撤销");
    }
    return;
  }
  if (p.y < 54 || p.y >= 318) {
    say("已取消拖动");
    return;
  }
  bool ok = projection_valid &&
            (source >= 0 || drag_group >= 0
                 ? model_move(nodes, selected_nodes, drop_parent, drop_pos)
                 : model_add(nodes, kind, drop_parent, drop_pos) >= 0);
  if (ok) {
    commit();
    memset(selected_nodes, 0, sizeof selected_nodes);
    schedule_render();
    say(drop_parent < 0 ? "已放入工作区" : "已放入循环");
  } else
    say("无法放入：最多 32 张、8 层，不能包含自身");
}
static void draggable(lv_obj_t *o, int data) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(o, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  lv_obj_add_event_cb(o, drag_event, LV_EVENT_ALL, (void *)(intptr_t)data);
}
static bool boxing, blank_moved;
static lv_point_t box_start;
static bool box_initial[LIMIT];
static bool intersects(lv_area_t a, lv_area_t b) {
  return a.x1 <= b.x2 && a.x2 >= b.x1 && a.y1 <= b.y2 && a.y2 >= b.y1;
}
static void update_box_selection(lv_point_t p) {
  lv_area_t r = {p.x < box_start.x ? p.x : box_start.x,
                 p.y < box_start.y ? p.y : box_start.y,
                 p.x > box_start.x ? p.x : box_start.x,
                 p.y > box_start.y ? p.y : box_start.y};
  lv_obj_set_pos(selection_box, r.x1, r.y1);
  lv_obj_set_size(selection_box, r.x2 - r.x1 + 1, r.y2 - r.y1 + 1);
  for (int i = 0; i < LIMIT; i++)
    if (cards[i]) {
      lv_area_t c;
      lv_obj_get_coords(cards[i], &c);
      bool hit = intersects(r, c);
      if (is_loop(nodes[i].type)) {
        lv_area_t beam = c, left = c, tail = c;
        beam.y2 = beam.y1 + 13;
        left.x2 = left.x1 + 33;
        tail.x1 = tail.x1 + sizes[i].width - 60;
        hit = intersects(r, beam) || intersects(r, left) || intersects(r, tail);
      }
      selected_nodes[i] = box_initial[i] || hit;
    }
  update_selection_outline();
}
static void canvas_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *in = lv_indev_active();
  if (!in)
    return;
  lv_point_t p;
  lv_indev_get_point(in, &p);
  if (code == LV_EVENT_PRESSED) {
    box_start = p;
    boxing = false;
    blank_moved = false;
    lv_indev_set_long_press_time(in, 500);
    memcpy(box_initial, selected_nodes, sizeof box_initial);
  }
  if (code == LV_EVENT_PRESSING && !boxing &&
      ((p.x - box_start.x) * (p.x - box_start.x) +
           (p.y - box_start.y) * (p.y - box_start.y) >
       100))
    blank_moved = true;
  if (code == LV_EVENT_LONG_PRESSED && !dragging && !blank_moved && !boxing) {
    boxing = true;
    multi = true;
    lv_obj_set_scroll_dir(canvas, LV_DIR_NONE);
    selection_box = decoration(lv_layer_top(), p.x, p.y, 1, 1, 0x76aaff);
    lv_obj_set_style_opa(selection_box, LV_OPA_30, 0);
    say("拖动框选，松手后可整组移动");
  }
  if (code == LV_EVENT_PRESSING && boxing)
    update_box_selection(p);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (boxing) {
      if (code == LV_EVENT_RELEASED)
        update_box_selection(p);
      else
        memcpy(selected_nodes, box_initial, sizeof box_initial);
      lv_obj_delete(selection_box);
      selection_box = NULL;
      boxing = false;
      lv_obj_set_scroll_dir(canvas, LV_DIR_ALL);
      schedule_render();
    } else if (code == LV_EVENT_RELEASED && !blank_moved) {
      memset(selected_nodes, 0, sizeof selected_nodes);
      multi = false;
      schedule_render();
      say("已取消选择");
    }
  }
}
static void category_change(lv_event_t *e) {
  category = (int)(intptr_t)lv_event_get_user_data(e);
  render_palette();
}
static void render_palette(void) {
  lv_obj_clean(palette);
  lv_obj_clean(tabs);
  const char *names[] = {"电机", "组合电机", "传感", "逻辑", "声光", "暂存"};
  uint32_t colors[] = {0x0090f5, 0xff4ccd, 0x34ccf1,
                       0xffb515, 0xc061f1, 0x6883a2};
  for (int i = 0; i < 6; i++) {
    lv_obj_t *b =
        button(tabs, 4 + i * 132, 1, 128, 44, names[i], category_change, i);
    lv_obj_set_style_bg_color(
        b, lv_color_hex(category == i ? colors[i] : 0xffffff), 0);
    if (category == i)
      lv_obj_set_style_text_color(b, lv_color_white(), 0);
  }
  if (category == 5) {
    int x = 14;
    for (int g = 0; g < GROUPS; g++)
      if (count(-2 - g)) {
        char text[48];
        int total = 0;
        for (int i = 0; i < LIMIT; i++)
          if (nodes[i].type) {
            int p = i;
            while (nodes[p].parent >= 0)
              p = nodes[p].parent;
            if (nodes[p].parent == -2 - g)
              total++;
          }
        snprintf(text, sizeof text, "组合 %d · %d 张", g + 1, total);
        lv_obj_t *b = box(palette, x, 8, 174, 70, 0xd4e4f9);
        label(b, text);
        draggable(b, -100 - g);
        x += 188;
      }
    if (x == 14) {
      lv_obj_t *hint = label(palette, "长按空白框选；拖到下方停留一秒暂存");
      place_label(hint, 145, 30);
    }
    return;
  }
  int x = 16;
  for (int t = 1; t <= TYPES; t++)
    if (blocks[t].category == category) {
      Node n = node_default(t);
      bool loop = is_loop(t);
      int w = loop ? 150 : 118;
      lv_obj_t *b = box(palette, x, 2, w, 86, blocks[t].color);
      lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
      draggable(b, -t);
      if (loop) {
        /* Scale the images directly: a transformed container clipped the
         * unscaled loop against the bottom of the palette before scaling. */
        const int scale = 160;
        int left = (w - 152 * scale / 256) / 2;
        lv_obj_t *shape = block_shape(b, 142, 50, true, false, blocks[t].color);
        if (shape) {
          lv_image_set_pivot(shape, 0, 0);
          lv_image_set_scale(shape, scale);
          lv_obj_set_pos(shape, left, 4);
        }
        icon(b, n, left + 93 * scale / 256, 4 + 29 * scale / 256);
        lv_obj_t *symbol = lv_obj_get_child(b, -1);
        lv_image_set_pivot(symbol, 0, 0);
        lv_image_set_scale(symbol, scale);
      } else {
        /* Palette artwork is 75% size; the full-size drag target is unchanged.
         */
        int body = loop ? 142 : 76;
        lv_obj_t *art = box(b, (w - (body + 10) * 3 / 4) / 2, 1, body + 10,
                            loop ? 88 : 74, blocks[t].color);
        lv_obj_set_style_bg_opa(art, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(art, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(art, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_style_transform_pivot_x(art, 0, 0);
        lv_obj_set_style_transform_pivot_y(art, 0, 0);
        lv_obj_set_style_transform_scale_x(art, 192, 0);
        lv_obj_set_style_transform_scale_y(art, 192, 0);
        lv_obj_t *shape = block_shape(art, body, loop ? 50 : 36, loop, false,
                                      blocks[t].color);
        if (shape)
          lv_obj_set_pos(shape, 0, 0);
        icon(art, n, loop ? 93 : 19, loop ? 29 : 17);
      }
      lv_obj_t *tag = box(b, 0, 66, w, 20, 0xeaf0f8);
      lv_obj_remove_flag(tag, LV_OBJ_FLAG_CLICKABLE);
      label(tag, loop ? (t == LOOP ? "次数" : "持续") : blocks[t].name);
      x += w + 18;
    }
}
static void render(void) {
  if (!projection_drawing) {
    selection_outline = NULL;
    selection_background = NULL;
  }
  lv_obj_clean(content);
  memset(cards, 0, sizeof cards);
  memset(cavities, 0, sizeof cavities);
  memset(bubbles, 0, sizeof bubbles);
  Size seq = sequence_size(-1);
  int y = seq.anchor < 112 ? 112 - seq.anchor : 24;
  lv_obj_t *start =
      decoration(content, 24, y + seq.anchor - 36, 68 + 8, 72, 0xf5c402);
  lv_obj_set_style_bg_opa(start, LV_OPA_TRANSP, 0);
  block_shape(start, 68, 36, false, true, 0xf5c402);
  label(start, LV_SYMBOL_PLAY);
  int x = 92;
  for (int j = 0; j < count(-1); j++) {
    int id = nth(-1, j);
    draw_node(content, id, x, y + seq.anchor - sizes[id].anchor);
    x += sizes[id].width;
  }
  lv_obj_set_size(content, x + 100 > 800 ? x + 100 : 800,
                  seq.height + y + 60 > 264 ? seq.height + y + 60 : 264);
  lv_obj_update_layout(content);
  lv_area_t occupied[LIMIT];
  int used = 0, extra_height = lv_obj_get_height(content),
      extra_width = lv_obj_get_width(content);
  lv_area_t origin;
  lv_obj_get_coords(content, &origin);
  for (int i = 0; i < LIMIT; i++)
    if (bubbles[i]) {
      lv_area_t r;
      lv_obj_get_coords(bubbles[i], &r);

      bool again = true;
      while (again) {
        again = false;
        for (int j = 0; j < used; j++)
          if (r.x1 < occupied[j].x2 + 4 && r.x2 + 4 > occupied[j].x1 &&
              r.y1 < occupied[j].y2 + 4 && r.y2 + 4 > occupied[j].y1) {
            int dy = occupied[j].y2 + 5 - r.y1;
            r.y1 += dy;
            r.y2 += dy;

            again = true;
          }
      }
      lv_obj_set_parent(bubbles[i], content);
      lv_obj_set_pos(bubbles[i], r.x1 - origin.x1, r.y1 - origin.y1);
      occupied[used++] = r;
      if (r.y2 - origin.y1 + 30 > extra_height)
        extra_height = r.y2 - origin.y1 + 30;
      if (r.x2 - origin.x1 + 30 > extra_width)
        extra_width = r.x2 - origin.x1 + 30;
    }
  lv_obj_set_size(content, extra_width, extra_height);
  if (!count(-1)) {
    lv_obj_t *hint = label(content, "从下方拖入积木开始编程");
    place_label(hint, 116, 100);
  }
  if (!projection_drawing)
    update_selection_outline();
  if (undo_btn) {
    if (history_pos == 0)
      lv_obj_add_state(undo_btn, LV_STATE_DISABLED);
    else
      lv_obj_remove_state(undo_btn, LV_STATE_DISABLED);
    if (history_pos == history_end)
      lv_obj_add_state(redo_btn, LV_STATE_DISABLED);
    else
      lv_obj_remove_state(redo_btn, LV_STATE_DISABLED);
  }
}
static void clear_confirm(lv_event_t *e) {
  (void)e;
  memset(nodes, 0, sizeof nodes);
  memset(selected_nodes, 0, sizeof selected_nodes);
  commit();
  close_modal(NULL);
  schedule_render();
  say("工作区和暂存已清空，可撤销");
}
static void clear(lv_event_t *e) {
  (void)e;
  modal = box(lv_screen_active(), 0, 0, 800, 480, 0x263744);
  lv_obj_set_style_bg_opa(modal, LV_OPA_50, 0);
  lv_obj_t *p = box(modal, 150, 130, 500, 210, 0xeaf0f8);
  lv_obj_t *t = label(p, "清空工作区和暂存？可撤销恢复");
  place_label(t, 24, 35);
  button(p, 30, 130, 190, 52, "取消", close_modal, 0);
  button(p, 280, 130, 190, 52, "清空", clear_confirm, 0);
}
static lv_obj_t *toolbar_button(lv_obj_t *bar, int x,
                                const lv_image_dsc_t *image, lv_event_cb_t cb,
                                int data) {
  lv_obj_t *b = box(bar, x, 5, 48, 44, 0xffffff);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)data);
  lv_obj_set_style_opa(b, LV_OPA_30, LV_STATE_DISABLED);
  lv_obj_set_style_bg_color(b, lv_color_hex(0xeaf0f8), LV_STATE_PRESSED);
  lv_obj_t *icon = lv_image_create(b);
  lv_image_set_src(icon, image);
  lv_obj_center(icon);
  lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  return b;
}
static void open_editor(void) {
  modal = NULL;
  ghost = NULL;
  marker = NULL;
  undo_btn = NULL;
  lv_obj_clean(lv_screen_active());
  history_reset();
  category = 0;
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0xeeeeee), 0);
  lv_obj_set_style_text_font(screen, &board_font, 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(0x263744), 0);
  lv_obj_t *bar = box(screen, 0, 0, 800, 54, 0xffffff);
  lv_obj_t *title = label(bar, library.items[active_project].name);
  place_label(title, 72, 16);
  lv_obj_set_width(title, 480);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  toolbar_button(bar, 8, &toolbar_home, back_home, 0);
  undo_btn = toolbar_button(bar, 576, &toolbar_undo, history_step, -1);
  redo_btn = toolbar_button(bar, 632, &toolbar_redo, history_step, 1);
  toolbar_button(bar, 688, &toolbar_save, save_work, 0);
  toolbar_button(bar, 744, &toolbar_clear, clear, 0);
  canvas = box(screen, 0, 54, 800, 264, 0xeeeeee);
  lv_obj_add_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(canvas, LV_DIR_ALL);
  lv_obj_set_scrollbar_mode(canvas, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(canvas, canvas_event, LV_EVENT_ALL, NULL);
  content = box(canvas, 0, 0, 800, 264, 0xeeeeee);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_CLICKABLE);
  tabs = box(screen, 0, 318, 800, 46, 0xeaf0f8);
  palette = box(screen, 0, 364, 800, 90, 0xeaf0f8);
  lv_obj_add_flag(palette, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(palette, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(palette, LV_SCROLLBAR_MODE_OFF);
  status = label(screen, "空白处滑动画布；长按框选，轻点取消选择");
  place_label(status, 8, 62);
  lv_obj_remove_flag(status, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(status, 775);
  lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
  render();
  render_palette();
}
#include "home.inc"
