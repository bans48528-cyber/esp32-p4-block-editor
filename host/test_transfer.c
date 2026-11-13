#include "ble_scan.h"
#include "projects.h"
#include "transfer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ESP_LOGI(...) ((void)0)
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define BLE_GATT_CHR_PROP_WRITE_NO_RSP 4
#define BLE_ERR_REM_USER_CONN_TERM 19
typedef unsigned TickType_t;
typedef struct {
  int kind;
  uint32_t generation;
} BleCommand;
struct ble_gatt_error {
  int status;
};
struct ble_gatt_attr {
  int unused;
};
static TransferSnapshot transfer;
static BleScanSnapshot state;
static int commands = 1, connection = 1, value_handle = 2, properties = 4;
static unsigned ticks;
static int mode, packets, last, ends, b9, d0, ba;
static TickType_t xTaskGetTickCount(void) { return ticks; }
static void vTaskDelay(unsigned n) { ticks += n; }
void ble_scan_snapshot(BleScanSnapshot *out) { *out = state; }
static unsigned ble_att_mtu(int conn) {
  (void)conn;
  return 512;
}
static int xQueueSend(int q, BleCommand *c, int t) {
  (void)q;
  (void)c;
  (void)t;
  return 1;
}
static int xQueueSendToFront(int q, BleCommand *c, int t) {
  return xQueueSend(q, c, t);
}
static int ble_gap_terminate(int conn, int reason) {
  (void)conn;
  (void)reason;
  state.link = BLE_LINK_OFF;
  return 0;
}
static int ble_gattc_write_no_rsp_flat(int, int, const void *, size_t);
static int ble_gattc_write_flat(int, int, const void *, size_t,
                                int (*)(uint16_t, const struct ble_gatt_error *,
                                        struct ble_gatt_attr *, void *),
                                void *);
#include "../main/transfer.inc"
static int ble_gattc_write_no_rsp_flat(int conn, int handle, const void *data,
                                       size_t n) {
  (void)conn;
  (void)handle;
  const uint8_t *p = data;
  assert(n == (size_t)p[3] + 7);
  packets++;
  last = p[4];
  if (last == 0xbc)
    ends++;
  if (last == 0xb9) {
    b9++;
    if (mode == 5 && b9 == 5) {
      state.device_report = DEVICE_REPORT_STOP;
      state.device_report_age_ms = 0;
      state.device_report_sequence++;
    }
  }
  if (last == 0xd0)
    d0++;
  if (last == 0xba)
    ba++;
  if (last == 0xda || last == 0xaa || last == 0xbc) {
    if (mode == 1)
      return 0; /* timeout */
    if (mode == 2) {
      rx_bad++;
      return 0;
    }
    if (mode == 3 && last == 0xaa) {
      cancel_transfer = true;
      return 0;
    }
    if (mode == 4) {
      state.link = BLE_LINK_OFF;
      return 0;
    }
    rx_ack++;
  }
  return 0;
}
static int ble_gattc_write_flat(int c, int h, const void *p, size_t n,
                                int (*cb)(uint16_t,
                                          const struct ble_gatt_error *,
                                          struct ble_gatt_attr *, void *),
                                void *arg) {
  int rc = ble_gattc_write_no_rsp_flat(c, h, p, n);
  struct ble_gatt_error error = {0};
  cb(c, &error, NULL, arg);
  return rc;
}
static void reset(int testmode) {
  mode = testmode;
  ticks = packets = last = ends = b9 = d0 = ba = rx_ack = rx_bad = 0;
  cancel_transfer = false;
  memset(&state, 0, sizeof state);
  state.link = BLE_LINK_READY;
  transfer.state = TRANSFER_IDLE;
  memset(transfer_nodes, 0, sizeof transfer_nodes);
  for (int i = 0; i < LIMIT; i++) {
    transfer_nodes[i] = node_default(WAIT);
    transfer_nodes[i].order = i;
    transfer_nodes[i].value = i + 1;
  }
}
int main(void) {
  reset(0);
  upload_program();
  assert(transfer.state == TRANSFER_SENT && transfer.done == transfer.total &&
         ends == 1 && last == 0xd0 && d0 == 2 && b9 == 0 && ba == 0);
  reset(0);
  state.device_report = DEVICE_REPORT_STOP;
  state.device_report_sequence = 1;
  upload_program();
  assert(transfer.state == TRANSFER_SENT && d0 == 1 && b9 == 0);
  reset(5);
  state.device_report = DEVICE_REPORT_RUN;
  state.device_report_sequence = 1;
  upload_program();
  assert(transfer.state == TRANSFER_SENT && b9 == 5 && ends == 1);
  reset(6);
  state.device_report = DEVICE_REPORT_RUN;
  state.device_report_sequence = 1;
  upload_program();
  assert(transfer.state == TRANSFER_ERROR && transfer.error == -208 &&
         b9 == 5 && !ends);
  reset(1);
  upload_program();
  assert(transfer.state == TRANSFER_ERROR && transfer.error == -205 && !ends &&
         packets == 2);
  reset(2);
  upload_program();
  assert(transfer.state == TRANSFER_ERROR && transfer.error == -204 && !ends);
  reset(3);
  upload_program();
  assert(cancel_transfer && !ends);
  assert(!simple_packet(0xb9) && last == 0xb9);
  reset(4);
  upload_program();
  assert(transfer.state == TRANSFER_ERROR && transfer.error == -201 && !ends);
  reset(0);
  properties = 0;
  upload_program();
  assert(transfer.state == TRANSFER_SENT && ends == 1);
  reset(0);
  state.device_report = DEVICE_REPORT_STOP;
  pause_program();
  assert(transfer.state == TRANSFER_STOPPED && transfer.total == 0 &&
         packets == 0);
  reset(0);
  pause_program();
  assert(transfer.state == TRANSFER_STOPPED && transfer.total == 1 && d0 == 1 &&
         b9 == 1 && ticks >= 200);
  reset(0);
  state.device_report = DEVICE_REPORT_RUN;
  pause_program();
  assert(transfer.state == TRANSFER_STOPPED && transfer.total == 5 && d0 == 0 &&
         b9 == 5 && ticks >= 320);
  puts("PASS: web-parity run preflight, five-B9 stop gate, waiting-state "
       "pause, upload/final-run, timeout/corruption/cancel/disconnect and both "
       "write modes");
}
