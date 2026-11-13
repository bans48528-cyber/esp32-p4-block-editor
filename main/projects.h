#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum {
  MOTOR = 1,
  WAIT,
  LOOP,
  REVERSE,
  MOTOR_STOP,
  FORWARD,
  BACKWARD,
  LEFT,
  RIGHT,
  COMBO_STOP,
  ULTRASONIC,
  GRAYSCALE,
  BUTTON_SENSOR,
  HOST_BUTTON,
  FOREVER,
  NOTE,
  MATRIX,
  MOTOR_POWER,
  MOTOR_CONTINUOUS,
  REVERSE_CONTINUOUS,
  COMBO_POWER,
  COMBO_CONTINUOUS,
  COMBO_DIRECTION,
  TYPES = 23,
  LIMIT = 100,
  PROJECTS = 6,
  GROUPS = 8,
  MAX_DEPTH = 8
};
typedef struct {
  int32_t type, parent, order, value, port, option;
  uint32_t matrix;
} Node;
typedef struct {
  uint32_t used;
  char name[64];
  Node nodes[LIMIT];
} Project;
typedef struct {
  uint32_t magic, version;
  Project items[PROJECTS];
} Library;
typedef struct {
  const char *name;
  int category;
  uint32_t color;
  int min, max, initial;
  bool decimal, port;
  int choices;
} BlockDef;
extern const BlockDef blocks[TYPES + 1];
bool is_loop(int type);
Node node_default(int type);
int model_count(const Node *n, int parent);
int model_nth(const Node *n, int parent, int order);
bool model_descendant(const Node *n, int id, int ancestor);
bool model_valid(const Node *n);
int model_add(Node *n, int type, int parent, int pos);
bool model_move(Node *n, const bool *selection, int parent, int pos);
void model_delete(Node *n, const bool *selection);
void projects_empty(Library *l);
bool projects_valid(const Library *l);
bool projects_decode(Library *l, const void *data, size_t size);
int projects_load(Library *l);
bool projects_save(const Library *l);
