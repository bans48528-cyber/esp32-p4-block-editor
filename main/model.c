#include "projects.h"
#include <string.h>
const BlockDef blocks[TYPES + 1] = {
    {0},
    {"电机正转", 0, 0x0090f5, 1, 9999, 10, true, true, 0},
    {"等待时间", 3, 0xffb515, 1, 9999, 10, true, false, 0},
    {"循环次数", 3, 0xffb515, 1, 9999, 1, false, false, 0},
    {"电机反转", 0, 0x0090f5, 1, 9999, 10, true, true, 0},
    {"电机停止", 0, 0x0090f5, 0, 0, 0, false, true, 0},
    {"组合前进", 1, 0xff4ccd, 1, 9999, 10, true, false, 0},
    {"组合后退", 1, 0xff4ccd, 1, 9999, 10, true, false, 0},
    {"组合左转", 1, 0xff4ccd, 1, 9999, 10, true, false, 0},
    {"组合右转", 1, 0xff4ccd, 1, 9999, 10, true, false, 0},
    {"组合停止", 1, 0xff4ccd, 0, 0, 0, false, false, 0},
    {"超声波", 2, 0x34ccf1, 0, 100, 10, false, false, 3},
    {"灰度传感器", 2, 0x34ccf1, 0, 1000, 500, false, false, 3},
    {"按键传感器", 2, 0x34ccf1, 0, 0, 0, false, false, 2},
    {"主机按键", 2, 0x34ccf1, 0, 0, 0, false, false, 2},
    {"持续循环", 3, 0xffb515, 0, 0, 0, false, false, 0},
    {"演奏音", 4, 0xc061f1, 0, 0, 0, false, false, 5},
    {"点阵", 4, 0x9b6af6, 0, 0, 0, false, false, 0}};
bool is_loop(int type) { return type == LOOP || type == FOREVER; }
Node node_default(int type) {
  Node n = {0};
  if (type < 1 || type > TYPES)
    return n;
  n.type = type;
  n.parent = -1;
  n.value = blocks[type].initial;
  n.matrix = type == MATRIX ? 1u << 12 : 0;
  return n;
}
int model_count(const Node *n, int p) {
  int c = 0;
  for (int i = 0; i < LIMIT; i++)
    if (n[i].type && n[i].parent == p)
      c++;
  return c;
}
int model_nth(const Node *n, int p, int o) {
  for (int i = 0; i < LIMIT; i++)
    if (n[i].type && n[i].parent == p && n[i].order == o)
      return i;
  return -1;
}
bool model_descendant(const Node *n, int id, int ancestor) {
  for (int d = 0; id >= 0 && id < LIMIT && d <= LIMIT; d++) {
    if (id == ancestor)
      return true;
    id = n[id].parent;
  }
  return false;
}
bool model_valid(const Node *n) {
  for (int i = 0; i < LIMIT; i++)
    if (n[i].type) {
      Node v = n[i];
      if (v.type < 1 || v.type > TYPES || v.parent < -1 - GROUPS ||
          v.parent >= LIMIT || v.port < 0 || v.port > 3 || v.matrix >> 25)
        return false;
      const BlockDef *b = &blocks[v.type];
      if (v.value < b->min || v.value > b->max || v.option < 0 ||
          v.option >= (b->choices ? b->choices : 1))
        return false;
      if (v.order < 0 || v.order >= model_count(n, v.parent))
        return false;
      for (int j = 0; j < i; j++)
        if (n[j].type && n[j].parent == v.parent && n[j].order == v.order)
          return false;
      int p = v.parent, depth = 0;
      while (p >= 0) {
        if (p >= LIMIT || p == i || !is_loop(n[p].type) || ++depth >= MAX_DEPTH)
          return false;
        p = n[p].parent;
      }
    }
  return true;
}
static void normalize(Node *n) {
  for (int p = -1 - GROUPS; p < LIMIT; p++) {
    int ids[LIMIT], c = 0;
    for (int i = 0; i < LIMIT; i++)
      if (n[i].type && n[i].parent == p)
        ids[c++] = i;
    for (int a = 0; a < c; a++)
      for (int b = a + 1; b < c; b++)
        if (n[ids[a]].order > n[ids[b]].order) {
          int t = ids[a];
          ids[a] = ids[b];
          ids[b] = t;
        }
    for (int i = 0; i < c; i++)
      n[ids[i]].order = i;
  }
}
int model_add(Node *n, int type, int parent, int pos) {
  if (type < 1 || type > TYPES || parent < -1 - GROUPS || parent >= LIMIT ||
      pos < 0 || pos > model_count(n, parent))
    return -1;
  Node copy[LIMIT];
  memcpy(copy, n, sizeof copy);
  int id = -1;
  for (int i = 0; i < LIMIT; i++)
    if (!copy[i].type) {
      id = i;
      break;
    }
  if (id < 0)
    return -1;
  for (int i = 0; i < LIMIT; i++)
    if (copy[i].type && copy[i].parent == parent && copy[i].order >= pos)
      copy[i].order++;
  copy[id] = node_default(type);
  copy[id].parent = parent;
  copy[id].order = pos;
  if (!model_valid(copy))
    return -1;
  memcpy(n, copy, sizeof copy);
  return id;
}
bool model_move(Node *n, const bool *selection, int parent, int pos) {
  if (parent < -1 - GROUPS || parent >= LIMIT || pos < 0 ||
      pos > model_count(n, parent))
    return false;
  Node copy[LIMIT];
  memcpy(copy, n, sizeof copy);
  int ids[LIMIT], c = 0, removed_before = 0;
  /* Preorder preserves visual order even when selected nodes span containers.
   */
  int rank[LIMIT];
  for (int i = 0; i < LIMIT; i++) {
    rank[i] = i;
  }
  /* Build a stable tree order using the ancestor/order path. */
  for (int i = 0; i < LIMIT; i++)
    for (int j = i + 1; j < LIMIT; j++) {
      int a = rank[i], b = rank[j], pa[MAX_DEPTH + 2], pb[MAX_DEPTH + 2],
          na = 0, nb = 0;
      for (int q = a; q >= 0 && na < MAX_DEPTH + 1; q = n[q].parent)
        pa[na++] = q;
      for (int q = b; q >= 0 && nb < MAX_DEPTH + 1; q = n[q].parent)
        pb[nb++] = q;
      int ia = na - 1, ib = nb - 1;
      while (ia >= 0 && ib >= 0 && pa[ia] == pb[ib]) {
        ia--;
        ib--;
      }
      bool swap = ia >= 0 && ib < 0;
      if (ia >= 0 && ib >= 0)
        swap = n[pa[ia]].parent != n[pb[ib]].parent
                   ? n[pa[ia]].parent > n[pb[ib]].parent
                   : n[pa[ia]].order > n[pb[ib]].order;
      if (swap) {
        rank[i] = b;
        rank[j] = a;
      }
    }
  for (int r = 0; r < LIMIT; r++) {
    int i = rank[r];
    if (!n[i].type || !selection[i])
      continue;
    bool child = false;
    for (int j = 0; j < LIMIT; j++)
      if (i != j && selection[j] && n[j].type && model_descendant(n, i, j))
        child = true;
    if (child)
      continue;
    if (parent >= 0 && model_descendant(n, parent, i))
      return false;
    ids[c++] = i;
    if (n[i].parent == parent && n[i].order < pos)
      removed_before++;
  }
  if (!c)
    return false;
  pos -= removed_before;
  for (int j = 0; j < c; j++)
    copy[ids[j]].type = 0;
  normalize(copy);
  for (int i = 0; i < LIMIT; i++)
    if (copy[i].type && copy[i].parent == parent && copy[i].order >= pos)
      copy[i].order += c;
  for (int j = 0; j < c; j++) {
    copy[ids[j]] = n[ids[j]];
    copy[ids[j]].parent = parent;
    copy[ids[j]].order = pos + j;
  }
  if (!model_valid(copy))
    return false;
  memcpy(n, copy, sizeof copy);
  return true;
}
void model_delete(Node *n, const bool *s) {
  bool removed[LIMIT] = {0};
  for (int i = 0; i < LIMIT; i++)
    for (int j = 0; j < LIMIT; j++)
      if (s[j] && n[j].type && model_descendant(n, i, j))
        removed[i] = true;
  for (int i = 0; i < LIMIT; i++)
    if (removed[i])
      memset(&n[i], 0, sizeof(Node));
  normalize(n);
}
