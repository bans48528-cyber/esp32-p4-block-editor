#include "ble_scan.h"
#include "device_status.h"
#include "transfer.h"
#include <stdlib.h>
static TransferSnapshot transfer;
#include <stdio.h>
#include <string.h>
static BleScanSnapshot state;
bool ble_scan_record(BleScanSnapshot *s, const uint8_t address[6], uint8_t type,
                     int8_t rssi, const uint8_t *data, size_t size) {
  char name[32] = {0};
  bool service = false;
  for (size_t pos = 0; pos < size;) {
    unsigned len = data[pos++];
    if (!len)
      break;
    if (len > size - pos)
      return false;
    uint8_t tag = data[pos];
    if ((tag == 8 || tag == 9) && len > 1) {
      size_t n = len - 1;
      if (n > 31)
        n = 31;
      for (size_t i = 0; i < n; i++)
        name[i] = (data[pos + 1 + i] >= 32 && data[pos + 1 + i] < 127)
                      ? data[pos + 1 + i]
                      : '?';
      name[n] = 0;
    }
    if (tag == 2 || tag == 3)
      for (unsigned i = 1; i + 1 < len; i += 2)
        if (data[pos + i] == 0xf0 && data[pos + i + 1] == 0xff)
          service = true;
    pos += len;
  }
  int id = -1;
  for (int i = 0; i < s->count; i++)
    if (s->devices[i].address_type == type &&
        !memcmp(address, s->devices[i].address, 6)) {
      id = i;
      break;
    }
  if (id < 0 && !name[0] && !service)
    return false;
  bool spark = !strcmp(name, "Spark_AI");
  if (id < 0) {
    if (s->count == BLE_SCAN_CAPACITY) {
      if (!spark && !service)
        return false;
      for (int i = s->count - 1; i >= 0; i--)
        if (!s->devices[i].spark && !s->devices[i].service_match) {
          id = i;
          break;
        }
      if (id < 0)
        return false;
    } else
      id = s->count++;
    memset(&s->devices[id], 0, sizeof s->devices[id]);
    memcpy(s->devices[id].address, address, 6);
    s->devices[id].address_type = type;
  }
  BleScanDevice *d = &s->devices[id];
  if (name[0])
    memcpy(d->name, name, sizeof name);
  d->rssi = rssi;
  d->spark = !strcmp(d->name, "Spark_AI");
  d->service_match |= service;
  s->revision++;
  return true;
}
#ifdef ESP_PLATFORM
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
static uint32_t device_report_at;
/* Only the NimBLE host callback parses this copy; do not allocate or parse
 * JSON while holding the shared spinlock, or use 4 KiB of task stack. */
static char device_json[4097];
static portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
typedef struct {
  uint8_t kind;
  uint32_t generation;
  BleScanDevice peer;
} BleCommand;
static QueueHandle_t commands;
static uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
static uint16_t svc_start, svc_end, value_handle, value_end, cccd;
static uint8_t properties;
static TickType_t link_started;
static int gap_event(struct ble_gap_event *event, void *unused);
#include "transfer.inc"
static void link_status(BleLinkState link, int error) {
  portENTER_CRITICAL(&guard);
  if (link != BLE_LINK_READY || state.link != BLE_LINK_READY) {
    state.device_report = DEVICE_REPORT_UNKNOWN;
    device_report_at = 0;
    state.device_report_sequence = 0;
  }
  state.link = link;
  state.link_error = error;
  state.write_handle = link == BLE_LINK_READY ? value_handle : 0;
  state.revision++;
  portEXIT_CRITICAL(&guard);
  ESP_LOGI("ble_link", "state=%d error=%d handle=%u", link, error,
           value_handle);
}
static bool discovering(uint16_t conn) {
  return conn == connection && state.link == BLE_LINK_DISCOVERING;
}
static void link_fail(int error) {
  link_status(BLE_LINK_ERROR, error);
  if (connection != BLE_HS_CONN_HANDLE_NONE)
    ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
}
static int subscribed(uint16_t conn, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg) {
  (void)attr;
  (void)arg;
  if (!discovering(conn))
    return 0;
  if (error->status)
    link_fail(error->status);
  else {
    link_status(BLE_LINK_READY, 0);
    BleCommand cmd = {.kind = 6};
    xQueueSend(commands, &cmd, 0);
  }
  return 0;
}
static int descriptor_found(uint16_t conn, const struct ble_gatt_error *error,
                            uint16_t chr, const struct ble_gatt_dsc *dsc,
                            void *arg) {
  (void)chr;
  (void)arg;
  if (!discovering(conn))
    return 0;
  if (!error->status) {
    if (ble_uuid_u16(&dsc->uuid.u) == 0x2902)
      cccd = dsc->handle;
    return 0;
  }
  if (error->status != BLE_HS_EDONE) {
    link_fail(error->status);
    return 0;
  }
  if (!cccd) {
    link_fail(-102);
    return 0;
  }
  uint8_t enable[2] = {(properties & BLE_GATT_CHR_PROP_NOTIFY) ? 1 : 2, 0};
  int rc = ble_gattc_write_flat(conn, cccd, enable, 2, subscribed, NULL);
  if (rc)
    link_fail(rc);
  return 0;
}
static int characteristic_found(uint16_t conn,
                                const struct ble_gatt_error *error,
                                const struct ble_gatt_chr *chr, void *arg) {
  (void)arg;
  if (!discovering(conn))
    return 0;
  if (!error->status) {
    if (value_handle && chr->def_handle > value_handle &&
        chr->def_handle - 1 < value_end)
      value_end = chr->def_handle - 1;
    if (!value_handle && ble_uuid_u16(&chr->uuid.u) == 0xfff1) {
      value_handle = chr->val_handle;
      properties = chr->properties;
    }
    return 0;
  }
  if (error->status != BLE_HS_EDONE) {
    link_fail(error->status);
    return 0;
  }
  if (!value_handle ||
      !(properties &
        (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_WRITE_NO_RSP)) ||
      !(properties & (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE))) {
    link_fail(-101);
    return 0;
  }
  ESP_LOGI("ble_link", "FFF1 value=%u properties=0x%02x", value_handle,
           properties);
  int rc = ble_gattc_disc_all_dscs(conn, value_handle, value_end,
                                   descriptor_found, NULL);
  if (rc)
    link_fail(rc);
  return 0;
}
static int service_found(uint16_t conn, const struct ble_gatt_error *error,
                         const struct ble_gatt_svc *svc, void *arg) {
  (void)arg;
  if (!discovering(conn))
    return 0;
  if (!error->status) {
    if (!svc_start) {
      svc_start = svc->start_handle;
      svc_end = svc->end_handle;
    }
    return 0;
  }
  if (error->status != BLE_HS_EDONE) {
    link_fail(error->status);
    return 0;
  }
  if (!svc_start) {
    link_fail(-100);
    return 0;
  }
  ESP_LOGI("ble_link", "FFF0 service=%u..%u", svc_start, svc_end);
  value_end = svc_end;
  int rc = ble_gattc_disc_all_chrs(conn, svc_start, svc_end,
                                   characteristic_found, NULL);
  if (rc)
    link_fail(rc);
  return 0;
}
static int mtu_exchanged(uint16_t conn, const struct ble_gatt_error *error,
                         uint16_t mtu, void *arg) {
  (void)arg;
  if (!discovering(conn))
    return 0;
  ESP_LOGI("ble_link", "MTU=%u status=%d", mtu, error->status);
  ble_uuid16_t uuid = BLE_UUID16_INIT(0xfff0);
  int rc = ble_gattc_disc_svc_by_uuid(conn, &uuid.u, service_found, NULL);
  if (rc)
    link_fail(rc);
  return 0;
}
static volatile bool synced;
static bool launched;
static void status(BleScanState value, int error) {
  portENTER_CRITICAL(&guard);
  state.state = value;
  state.error = error;
  state.revision++;
  portEXIT_CRITICAL(&guard);
  ESP_LOGI("ble_scan", "state=%d error=%d", value, error);
}
void ble_scan_snapshot(BleScanSnapshot *out) {
  portENTER_CRITICAL(&guard);
  *out = state;
  out->device_report_age_ms =
      (uint32_t)(esp_timer_get_time() / 1000) - device_report_at;
  portEXIT_CRITICAL(&guard);
}
static int gap_event(struct ble_gap_event *event, void *unused) {
  (void)unused;
  if (event->type == BLE_GAP_EVENT_CONNECT) {
    if (event->connect.status) {
      if (state.link == BLE_LINK_CONNECTING)
        link_status(BLE_LINK_ERROR, event->connect.status);
      return 0;
    }
    if (state.link != BLE_LINK_CONNECTING) {
      ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
      return 0;
    }
    connection = event->connect.conn_handle;
    svc_start = svc_end = value_handle = value_end = cccd = 0;
    properties = 0;
    link_status(BLE_LINK_DISCOVERING, 0);
    memset(&receiver, 0, sizeof receiver);
    int rc = ble_gattc_exchange_mtu(connection, mtu_exchanged, NULL);
    if (rc)
      link_fail(rc);
    return 0;
  }
  if (event->type == BLE_GAP_EVENT_DISCONNECT) {
    if (event->disconnect.conn.conn_handle == connection) {
      connection = BLE_HS_CONN_HANDLE_NONE;
      if (state.link != BLE_LINK_ERROR)
        link_status(BLE_LINK_OFF, event->disconnect.reason);
      ESP_LOGI("ble_link", "disconnected reason=%d", event->disconnect.reason);
    }
    return 0;
  }
  if (event->type == BLE_GAP_EVENT_NOTIFY_RX) {
    if (event->notify_rx.conn_handle == connection &&
        event->notify_rx.attr_handle == value_handle) {
      portENTER_CRITICAL(&guard);
      for (struct os_mbuf *m = event->notify_rx.om; m;
           m = SLIST_NEXT(m, om_next))
        for (unsigned i = 0; i < m->om_len; i++) {
          int result = spark_rx_byte(&receiver, m->om_data[i]);
          if (result == 1)
            rx_ack++;
          else if (result == 2) {
            memcpy(device_json, receiver.json, receiver.json_size + 1);
            portEXIT_CRITICAL(&guard);
            DeviceReport report = device_report_parse(device_json);
            uint32_t received_at = (uint32_t)(esp_timer_get_time() / 1000);
            portENTER_CRITICAL(&guard);
            if (state.link == BLE_LINK_READY &&
                event->notify_rx.conn_handle == connection) {
              state.device_report = report;
              device_report_at = received_at;
              if (report != DEVICE_REPORT_UNKNOWN)
                state.device_report_sequence++;
            }
          } else if (result < 0) {
            rx_bad++;
            state.device_report = DEVICE_REPORT_UNKNOWN;
          }
        }
      state.notifications++;
      state.revision++;
      portEXIT_CRITICAL(&guard);
    }
    return 0;
  }
  if (event->type == BLE_GAP_EVENT_DISC) {
    portENTER_CRITICAL(&guard);
    bool changed = ble_scan_record(&state, event->disc.addr.val,
                                   event->disc.addr.type, event->disc.rssi,
                                   event->disc.data, event->disc.length_data);
    portEXIT_CRITICAL(&guard);
    if (changed)
      ESP_LOGD("ble_scan", "advertisement received rssi=%d", event->disc.rssi);
  } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
    status(BLE_SCAN_DONE, 0);
    BleScanSnapshot result;
    ble_scan_snapshot(&result);
    for (int i = 0; i < result.count; i++)
      ESP_LOGI("ble_scan", "found name=%s rssi=%d Spark_AI=%d",
               result.devices[i].name, result.devices[i].rssi,
               result.devices[i].spark);
  }
  return 0;
}
static void on_sync(void) { synced = true; }
static void on_reset(int reason) {
  synced = false;
  connection = BLE_HS_CONN_HANDLE_NONE;
  link_status(BLE_LINK_ERROR, reason);
  status(BLE_SCAN_ERROR, reason);
}
static void host_task(void *p) {
  (void)p;
  nimble_port_run();
  nimble_port_freertos_deinit();
}
static void worker(void *unused) {
  (void)unused;
  int rc = esp_hosted_init();
  if (!rc)
    rc = esp_hosted_connect_to_slave();
  if (!rc) {
    esp_hosted_coprocessor_fwver_t version = {0};
    rc = esp_hosted_get_coprocessor_fwversion(&version);
    if (!rc) {
      ESP_LOGI("ble_scan", "C6 firmware %lu.%lu.%lu",
               (unsigned long)version.major1, (unsigned long)version.minor1,
               (unsigned long)version.patch1);
      /* Hosted <2.5.2 enables the controller at boot and has no init RPC. */
      bool legacy =
          version.major1 < 2 ||
          (version.major1 == 2 &&
           (version.minor1 < 5 || (version.minor1 == 5 && version.patch1 < 2)));
      if (legacy) {
        ESP_LOGI("ble_scan", "Using legacy boot-enabled BLE controller");
      } else {
        rc = esp_hosted_bt_controller_init();
        if (!rc)
          rc = esp_hosted_bt_controller_enable();
      }
    }
  }
  if (!rc)
    rc = nimble_port_init();
  if (rc) {
    status(BLE_SCAN_ERROR, rc);
    vTaskDelete(NULL);
    return;
  }
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.reset_cb = on_reset;
  ble_att_set_preferred_mtu(512);
  nimble_port_freertos_init(host_task);
  for (int i = 0; i < 300 && !synced; i++)
    vTaskDelay(pdMS_TO_TICKS(100));
  if (!synced) {
    status(BLE_SCAN_ERROR, -1);
    vTaskDelete(NULL);
    return;
  }
  for (;;) {
    BleCommand command;
    if (xQueueReceive(commands, &command, pdMS_TO_TICKS(250)) != pdTRUE) {
      if ((state.link == BLE_LINK_CONNECTING ||
           state.link == BLE_LINK_DISCOVERING) &&
          (TickType_t)(xTaskGetTickCount() - link_started) >
              pdMS_TO_TICKS(15000)) {
        link_fail(-103);
        ble_gap_conn_cancel();
      }
      continue;
    }
    if (command.kind == 4) {
      if (command.generation == transfer_generation)
        upload_program();
      continue;
    }
    if (command.kind == 5) {
      pause_program();
      continue;
    }
    if (command.kind == 6) {
      simple_packet(0xd0);
      continue;
    }
    if (command.kind == 2) {
      if (connection != BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
      else {
        link_status(BLE_LINK_OFF, 0);
        ble_gap_conn_cancel();
      }
      continue;
    }
    if (command.kind == 3) {
      if (!synced || connection != BLE_HS_CONN_HANDLE_NONE ||
          ble_gap_conn_active())
        continue;
      if (ble_gap_disc_active())
        ble_gap_disc_cancel();
      uint8_t type;
      rc = ble_hs_id_infer_auto(0, &type);
      ble_addr_t peer = {.type = command.peer.address_type};
      memcpy(peer.val, command.peer.address, 6);
      link_started = xTaskGetTickCount();
      if (!rc)
        rc = ble_gap_connect(type, &peer, 10000, NULL, gap_event, NULL);
      if (rc)
        link_fail(rc);
      continue;
    }
    if (!command.kind) {
      if (ble_gap_disc_active())
        ble_gap_disc_cancel();
      status(BLE_SCAN_DONE, 0);
      continue;
    }
    if (ble_gap_disc_active() || state.link == BLE_LINK_CONNECTING ||
        state.link == BLE_LINK_DISCOVERING || state.link == BLE_LINK_READY)
      continue;
    if (!synced) {
      status(BLE_SCAN_ERROR, -2);
      continue;
    }
    uint8_t type;
    rc = ble_hs_util_ensure_addr(0);
    if (!rc)
      rc = ble_hs_id_infer_auto(0, &type);
    if (rc) {
      status(BLE_SCAN_ERROR, rc);
      continue;
    }
    portENTER_CRITICAL(&guard);
    state.count = 0;
    state.state = BLE_SCAN_RUNNING;
    state.error = 0;
    state.revision++;
    portEXIT_CRITICAL(&guard);
    struct ble_gap_disc_params params = {0};
    params.passive = 0;
    params.itvl = 160;
    params.window = 80;
    params.filter_duplicates = 0;
    rc = ble_gap_disc(type, 10000, &params, gap_event, NULL);
    if (rc)
      status(BLE_SCAN_ERROR, rc);
  }
}
void ble_scan_start(void) {
  if (!launched) {
    commands = xQueueCreate(8, sizeof(BleCommand));
    if (!commands) {
      status(BLE_SCAN_ERROR, -3);
      return;
    }
    status(BLE_SCAN_STARTING, 0);
    if (xTaskCreate(worker, "ble_scan", 32768, NULL, 4, NULL) != pdPASS) {
      vQueueDelete(commands);
      commands = NULL;
      status(BLE_SCAN_ERROR, -4);
      return;
    }
    launched = true;
  }
  BleCommand command = {.kind = 1};
  xQueueSend(commands, &command, 0);
}
void ble_scan_stop(void) {
  if (commands) {
    BleCommand command = {.kind = 0};
    xQueueSend(commands, &command, 0);
  }
}
bool ble_link_connect(const BleScanDevice *device) {
  if (!device || !device->spark || !commands || !synced ||
      state.link == BLE_LINK_READY || state.link == BLE_LINK_CONNECTING ||
      state.link == BLE_LINK_DISCOVERING)
    return false;
  BleCommand command = {.kind = 3, .peer = *device};
  portENTER_CRITICAL(&guard);
  state.peer = *device;
  state.notifications = 0;
  portEXIT_CRITICAL(&guard);
  link_started = xTaskGetTickCount();
  link_status(BLE_LINK_CONNECTING, 0);
  if (xQueueSend(commands, &command, 0) != pdTRUE) {
    link_status(BLE_LINK_ERROR, -104);
    return false;
  }
  return true;
}
void ble_link_disconnect(void) {
  if (commands) {
    BleCommand command = {.kind = 2};
    xQueueSend(commands, &command, 0);
  }
}
#else
void ble_program_snapshot(TransferSnapshot *out) { *out = transfer; }
bool ble_program_run(const Node *nodes) {
  (void)nodes;
  return false;
}
bool ble_program_stop(void) { return false; }
bool ble_link_connect(const BleScanDevice *device) {
  (void)device;
  return false;
}
void ble_link_disconnect(void) {}
void ble_scan_snapshot(BleScanSnapshot *out) { *out = state; }
void ble_scan_start(void) {
  state.state = BLE_SCAN_UNAVAILABLE;
  state.revision++;
}
void ble_scan_stop(void) {}
#endif
