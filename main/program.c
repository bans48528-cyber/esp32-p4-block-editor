#include "program.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef struct {
  char *out;
  size_t left;
  bool ok;
} Writer;
static void put(Writer *w, const char *fmt, ...) {
  if (!w->ok)
    return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(w->out, w->left, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= w->left) {
    w->ok = false;
    return;
  }
  w->out += n;
  w->left -= n;
}
static const char *dirs[] = {"advance", "retreat", "left", "right"};
static const char *ops[] = {"<", ">", "=="};
static void sequence(Writer *w, const Node *nodes, int parent, int depth) {
  for (int j = 0; j < model_count(nodes, parent); j++) {
    const Node *n = &nodes[model_nth(nodes, parent, j)];
    int t = n->type, value = n->value, port = n->port, option = n->option;
    char time[24];
    snprintf(time, sizeof time, "%d", value / 10);
    if (value % 10)
      snprintf(time, sizeof time, "%d.%d", value / 10, value % 10);
    put(w, "%*s", depth * 4, "");
    switch (t) {
    case MOTOR_POWER:
    case COMBO_POWER:
      put(w, "%s_power = %d", t == MOTOR_POWER ? "motor" : "combo",
          (option + 1) * 25);
      break;
    case MOTOR:
    case REVERSE:
      put(w, "_motor.run_for_power_seconds(%d, %smotor_power, %s)", port + 4,
          t == REVERSE ? "-" : "", time);
      break;
    case MOTOR_STOP:
      put(w, "_motor.stop(%d)", port + 4);
      break;
    case MOTOR_CONTINUOUS:
    case REVERSE_CONTINUOUS:
      put(w, "_motor.run_power(%d, %smotor_power)", port + 4,
          t == REVERSE_CONTINUOUS ? "-" : "");
      break;
    case FORWARD:
    case BACKWARD:
    case LEFT:
    case RIGHT:
      put(w, "_motor.mov_dir_power_seconds(\"%s\", combo_power, %s)",
          dirs[t - FORWARD], time);
      break;
    case COMBO_DIRECTION:
      put(w, "_motor.pair(4, 5, %d)", (const int[]){3, 0, 1, 2}[option]);
      break;
    case COMBO_CONTINUOUS:
      put(w, "_motor.mov_dir_power(\"%s\", combo_power)", dirs[option]);
      break;
    case COMBO_STOP:
      put(w, "_motor.mov_stop()");
      break;
    case WAIT:
      put(w, "_os.sleep_s(%s)", time);
      break;
    case NOTE:
      put(
          w, "_beep.play_muic(\"%c\", %s)", "cdefgab" [option],
          (const char *[]) { "0.25", "0.5", "1", "2" }[value]);
      break;
    case MATRIX:
      put(w, "_matrix.show(0x00");
      for (int y = 0; y < 5; y++)
        put(w, ", 0x%02X", (n->matrix >> (y * 5)) & 31);
      put(w, ", 0x00)");
      break;
    case ULTRASONIC:
    case GRAYSCALE:
    case BUTTON_SENSOR:
    case HOST_BUTTON:
      put(w, "while not (");
      if (t == ULTRASONIC || t == GRAYSCALE)
        put(w, "%s(%d, \"%s\", %d)",
            t == ULTRASONIC ? "_ultrasion.cmp_value" : "_color.cmp_lux", port,
            ops[option], value);
      else if (t == BUTTON_SENSOR)
        put(w, "%s_touch.state(%d)", option ? "not " : "", port);
      else
        put(w, "_key.key_mast(\"%s\", 1)", option ? "right" : "left");
      put(w, "):\n%*s_os.sleep_s(0.001)", (depth + 1) * 4, "");
      break;
    case LOOP:
    case FOREVER:
      if (t == LOOP)
        put(w, "for count_%d in range(%d):\n", depth, value);
      else
        put(w, "while True:\n");
      sequence(w, nodes, (int)(n - nodes), depth + 1);
      put(w, "%*s_os.sleep_s(0.001)", (depth + 1) * 4, "");
      break;
    default:
      w->ok = false;
      break;
    }
    put(w, "\n");
  }
}
bool program_source(const Node *nodes, char *out, size_t capacity) {
  if (!capacity)
    return false;
  out[0] = 0;
  if (!model_valid(nodes) || !model_count(nodes, -1))
    return false;
  bool motor = false, combo = false, power = false;
  for (int i = 0; i < LIMIT; i++) {
    if (!nodes[i].type)
      continue;
    int root = i;
    while (nodes[root].parent >= 0)
      root = nodes[root].parent;
    if (nodes[root].parent != -1)
      continue;
    int t = nodes[i].type;
    motor |= t == MOTOR || t == REVERSE || t == MOTOR_CONTINUOUS ||
             t == REVERSE_CONTINUOUS || t == MOTOR_POWER;
    bool movement = (t >= FORWARD && t <= RIGHT) || t == COMBO_CONTINUOUS;
    combo |= movement || t == COMBO_STOP || t == COMBO_DIRECTION;
    power |= movement || t == COMBO_POWER;
  }
  Writer w = {out, capacity, true};
  if (motor)
    put(&w, "motor_power = 50\n");
  if (power)
    put(&w, "combo_power = 50\n");
  if (combo)
    put(&w, "_motor.pair(4, 5, 1)\n_motor.mov_set_stop_module(1)\n");
  sequence(&w, nodes, -1, 0);
  if (!w.ok)
    out[0] = 0;
  return w.ok;
}
