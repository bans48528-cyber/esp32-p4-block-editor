/* Included by ble_scan.c: all writes are serialized in its worker. */
#include "compile.h"
#include "program.h"
#include "spark_protocol.h"
static Node transfer_nodes[LIMIT];
static volatile bool cancel_transfer;
static uint32_t transfer_generation;
static SparkRx receiver;
static uint32_t rx_ack, rx_bad;
static volatile uint32_t write_sequence, write_complete;
static volatile int write_result;
static void transfer_status(TransferState s, int error, unsigned done,
                            unsigned total) {
  portENTER_CRITICAL(&guard);
  transfer = (TransferSnapshot){s, done, total, error};
  portEXIT_CRITICAL(&guard);
  ESP_LOGI("spark_tx", "state=%d error=%d bytes=%u/%u", s, error, done, total);
}
void ble_program_snapshot(TransferSnapshot *out) {
  portENTER_CRITICAL(&guard);
  *out = transfer;
  portEXIT_CRITICAL(&guard);
}
static int write_done(uint16_t conn, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg) {
  (void)conn;
  (void)attr;
  if ((uint32_t)(uintptr_t)arg == write_sequence) {
    write_result = error->status;
    write_complete = write_sequence;
  }
  return 0;
}
static int send_packet(uint8_t cmd, const uint8_t *data, size_t size, bool ack,
                       bool cancellable) {
  if (state.link != BLE_LINK_READY)
    return -201;
  if (cancellable && cancel_transfer)
    return -202;
  uint8_t packet[262];
  size_t n = spark_frame(packet, cmd, data, size);
  if (n + 3 > ble_att_mtu(connection))
    return -203;
  uint32_t before, bad;
  portENTER_CRITICAL(&guard);
  before = rx_ack;
  bad = rx_bad;
  portEXIT_CRITICAL(&guard);
  uint32_t seq = ++write_sequence;
  int rc;
  if (properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP) {
    rc = ble_gattc_write_no_rsp_flat(connection, value_handle, packet, n);
    write_result = rc;
    write_complete = seq;
  } else
    rc = ble_gattc_write_flat(connection, value_handle, packet, n, write_done,
                              (void *)(uintptr_t)seq);
  if (rc)
    return rc;
  TickType_t began = xTaskGetTickCount();
  for (;;) {
    if (state.link != BLE_LINK_READY)
      return -201;
    /* Complete the outstanding ATT write before the priority STOP write. */
    if (cancellable && cancel_transfer && write_complete == seq)
      return -202;
    portENTER_CRITICAL(&guard);
    uint32_t current = rx_ack, broken = rx_bad;
    portEXIT_CRITICAL(&guard);
    if (broken != bad)
      return -204;
    if (write_complete == seq) {
      if (write_result)
        return write_result;
      if (!ack || current != before)
        return 0;
    }
    if (xTaskGetTickCount() - began > pdMS_TO_TICKS(5000))
      return -205;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
static int simple_packet(uint8_t cmd) {
  const uint8_t one = 1;
  return send_packet(cmd, &one, 1, false, false);
}
static int control_packet(uint8_t cmd, bool cancellable) {
  const uint8_t one = 1;
  return send_packet(cmd, &one, 1, false, cancellable);
}
static void invalidate_device_report(void) {
  portENTER_CRITICAL(&guard);
  state.device_report = DEVICE_REPORT_UNKNOWN;
  state.device_report_age_ms = 0;
  state.revision++;
  portEXIT_CRITICAL(&guard);
}
static bool transfer_delay(unsigned milliseconds, bool cancellable) {
  for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += 10) {
    if (state.link != BLE_LINK_READY || (cancellable && cancel_transfer))
      return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return true;
}
static bool fresh_stopped(BleScanSnapshot *snapshot) {
  ble_scan_snapshot(snapshot);
  return snapshot->device_report == DEVICE_REPORT_STOP &&
         snapshot->device_report_age_ms <= 2000;
}
static int stop_burst(bool cancellable) {
  invalidate_device_report();
  for (int i = 0; i < 5; i++) {
    if (i && !transfer_delay(80, cancellable))
      return cancellable && cancel_transfer ? -202 : -201;
    int error = control_packet(0xb9, cancellable);
    if (error)
      return error;
  }
  return 0;
}
static void pause_program(void) {
  BleScanSnapshot snapshot;
  ble_scan_snapshot(&snapshot);
  int result = 0;
  unsigned sent = 0;
  if (snapshot.device_report == DEVICE_REPORT_STOP &&
      snapshot.device_report_age_ms <= 2000) {
    transfer_status(TRANSFER_STOPPED, 0, 0, 0);
    return;
  }
  if (snapshot.device_report == DEVICE_REPORT_UNKNOWN) {
    result = simple_packet(0xd0);
    if (!result && transfer_delay(200, false)) {
      invalidate_device_report();
      result = simple_packet(0xb9);
      sent = result ? 0 : 1;
    }
  } else {
    result = stop_burst(false);
    sent = result ? 0 : 5;
  }
  transfer_status(result ? TRANSFER_ERROR : TRANSFER_STOPPED, result, sent,
                  sent);
}
static int prepare_run(bool *allow_waiting) {
  *allow_waiting = false;
  transfer_status(TRANSFER_PREPARING, 0, 0, 0);
  BleScanSnapshot snapshot;
  ble_scan_snapshot(&snapshot);
  if (snapshot.device_report == DEVICE_REPORT_UNKNOWN) {
    int error = control_packet(0xd0, true);
    if (error)
      return error;
    if (!transfer_delay(500, true))
      return cancel_transfer ? -202 : -201;
    ble_scan_snapshot(&snapshot);
    if (snapshot.device_report == DEVICE_REPORT_UNKNOWN) {
      *allow_waiting = true;
      return 0;
    }
  }
  if (fresh_stopped(&snapshot))
    return 0;
  uint32_t before = snapshot.device_report_sequence;
  int error = stop_burst(true);
  if (error)
    return error;
  for (unsigned waited = 0; waited < 5000; waited += 10) {
    ble_scan_snapshot(&snapshot);
    if (snapshot.device_report_sequence > before &&
        snapshot.device_report == DEVICE_REPORT_STOP)
      return 0;
    if (!transfer_delay(10, true))
      return cancel_transfer ? -202 : -201;
  }
  return -208;
}
static bool run_state_ready(bool allow_waiting) {
  BleScanSnapshot snapshot;
  ble_scan_snapshot(&snapshot);
  if (allow_waiting && snapshot.device_report == DEVICE_REPORT_UNKNOWN)
    return true;
  return snapshot.device_report == DEVICE_REPORT_STOP &&
         snapshot.device_report_age_ms <= 2000;
}
static void upload_program(void) {
  if (cancel_transfer)
    return;
  char *source = malloc(16384);
  uint8_t *code = malloc(32768);
  int error = 0;
  size_t size = 0, done = 0;
  bool allow_waiting = false;
  bool upload_started = false;
  error = prepare_run(&allow_waiting);
  if (!error)
    transfer_status(TRANSFER_COMPILING, 0, 0, 0);
  if (!source || !code)
    error = -206;
  else if (!error && (!program_source(transfer_nodes, source, 16384) ||
                      !(size = board_compile(source, code, 32768))))
    error = -207;
  free(source);
  if (!error && cancel_transfer)
    error = -202;
  if (!error && ble_att_mtu(connection) < 138)
    error = -203;
  if (!error && !run_state_ready(allow_waiting))
    error = -209;
  if (!error) {
    /* Keep D0 monitoring active. Status JSON and upload ACKs share the
     *
     * receiver, matching the web implementation. */
    invalidate_device_report();
    upload_started = true;
    const uint8_t name[] = {'0', '.', 'o'};
    error = send_packet(0xda, name, sizeof name, true, true);
  }
  while (!error && done < size) {
    size_t count = size - done;
    if (count > 128)
      count = 128;
    transfer_status(TRANSFER_UPLOADING, 0, (unsigned)done, (unsigned)size);
    error = send_packet(done + count == size ? 0xbc : 0xaa, code + done, count,
                        true, true);
    if (!error)
      done += count;
  }
  free(code);
  if (cancel_transfer || error == -202)
    return; /* Priority STOP command follows. */
  if (error) {
    transfer_status(TRANSFER_ERROR, error, (unsigned)done, (unsigned)size);
    /* Prevent late ACKs from contaminating another transfer after failure. */
    if (upload_started && state.link == BLE_LINK_READY)
      ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
  } else {
    int watch = simple_packet(0xd0);
    transfer_status(TRANSFER_SENT, watch, (unsigned)done, (unsigned)size);
  }
}
bool ble_program_run(const Node *nodes) {
  if (!commands || state.link != BLE_LINK_READY || !model_valid(nodes) ||
      !model_count(nodes, -1))
    return false;
  portENTER_CRITICAL(&guard);
  if (transfer.state == TRANSFER_PREPARING ||
      transfer.state == TRANSFER_COMPILING ||
      transfer.state == TRANSFER_UPLOADING ||
      transfer.state == TRANSFER_STOPPING) {
    portEXIT_CRITICAL(&guard);
    return false;
  }
  memcpy(transfer_nodes, nodes, sizeof transfer_nodes);
  cancel_transfer = false;
  transfer = (TransferSnapshot){.state = TRANSFER_PREPARING};
  portEXIT_CRITICAL(&guard);
  BleCommand cmd = {.kind = 4, .generation = ++transfer_generation};
  if (xQueueSend(commands, &cmd, 0) != pdTRUE) {
    transfer_status(TRANSFER_ERROR, -104, 0, 0);
    return false;
  }
  return true;
}
bool ble_program_stop(void) {
  if (!commands || state.link != BLE_LINK_READY)
    return false;
  cancel_transfer = true;
  ++transfer_generation;
  transfer_status(TRANSFER_STOPPING, 0, 0, 0);
  BleCommand cmd = {.kind = 5};
  if (xQueueSendToFront(commands, &cmd, 0) != pdTRUE) {
    transfer_status(TRANSFER_ERROR, -104, 0, 0);
    return false;
  }
  return true;
}
