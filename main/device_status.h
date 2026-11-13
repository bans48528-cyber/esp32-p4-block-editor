#pragma once
#include "ble_scan.h"
typedef enum {
  DEVICE_DISCONNECTED,
  DEVICE_CONNECTING,
  DEVICE_WAITING,
  DEVICE_STALE,
  DEVICE_RUNNING,
  DEVICE_STOPPED
} DeviceStatus;
DeviceReport device_report_parse(const char *json);
DeviceStatus device_status_view(const BleScanSnapshot *snapshot);
const char *device_status_text(DeviceStatus status);
uint32_t device_status_color(DeviceStatus status);
