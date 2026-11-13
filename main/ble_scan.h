#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BLE_SCAN_CAPACITY 16
typedef enum {
  BLE_SCAN_IDLE,
  BLE_SCAN_STARTING,
  BLE_SCAN_RUNNING,
  BLE_SCAN_DONE,
  BLE_SCAN_ERROR,
  BLE_SCAN_UNAVAILABLE
} BleScanState;
typedef struct {
  uint8_t address[6], address_type;
  char name[32];
  int8_t rssi;
  bool spark, service_match;
} BleScanDevice;
typedef enum {
  BLE_LINK_OFF,
  BLE_LINK_CONNECTING,
  BLE_LINK_DISCOVERING,
  BLE_LINK_READY,
  BLE_LINK_ERROR
} BleLinkState;
typedef enum {
  DEVICE_REPORT_UNKNOWN,
  DEVICE_REPORT_RUN,
  DEVICE_REPORT_STOP
} DeviceReport;
typedef struct {
  DeviceReport device_report;
  uint32_t device_report_age_ms, device_report_sequence;
  BleLinkState link;
  BleScanDevice peer;
  int link_error;
  uint16_t write_handle;
  uint32_t notifications;
  BleScanState state;
  int error, count;
  uint32_t revision;
  BleScanDevice devices[BLE_SCAN_CAPACITY];
} BleScanSnapshot;
void ble_scan_start(void);
void ble_scan_stop(void);
bool ble_link_connect(const BleScanDevice *device);
void ble_link_disconnect(void);
void ble_scan_snapshot(BleScanSnapshot *out);
/* Pure advertisement parser/merger, also used by host regression tests. */
bool ble_scan_record(BleScanSnapshot *s, const uint8_t address[6], uint8_t type,
                     int8_t rssi, const uint8_t *data, size_t size);
