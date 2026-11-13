#include "device_status.h"
#include "cJSON.h"
#include <string.h>
DeviceReport device_report_parse(const char *json) {
  if (!json || strlen(json) > 4096 || strstr(json, "\\u0000"))
    return DEVICE_REPORT_UNKNOWN;
  /* Bound recursive JSON parsing to the small NimBLE host task stack. */
  unsigned depth = 0;
  bool quoted = false, escape = false;
  for (const char *p = json; *p; p++) {
    if (escape) {
      escape = false;
      continue;
    }
    if (quoted && *p == '\\') {
      escape = true;
      continue;
    }
    if (*p == '"') {
      quoted = !quoted;
      continue;
    }
    if (!quoted) {
      if ((*p == '{' || *p == '[') && ++depth > 16)
        return DEVICE_REPORT_UNKNOWN;
      if (*p == '}' || *p == ']') {
        if (!depth)
          return DEVICE_REPORT_UNKNOWN;
        depth--;
      }
    }
  }
  cJSON *root = cJSON_ParseWithLengthOpts(json, strlen(json) + 1, NULL, 1);
  DeviceReport result = DEVICE_REPORT_UNKNOWN;
  if (cJSON_IsObject(root)) {
    cJSON *field = cJSON_GetObjectItemCaseSensitive(root, "WillAiState");
    if (cJSON_IsString(field)) {
      if (!strcmp(field->valuestring, "run"))
        result = DEVICE_REPORT_RUN;
      else if (!strcmp(field->valuestring, "stop"))
        result = DEVICE_REPORT_STOP;
    }
  }
  cJSON_Delete(root);
  return result;
}
DeviceStatus device_status_view(const BleScanSnapshot *s) {
  if (s->link == BLE_LINK_CONNECTING || s->link == BLE_LINK_DISCOVERING)
    return DEVICE_CONNECTING;
  if (s->link != BLE_LINK_READY)
    return DEVICE_DISCONNECTED;
  if (s->device_report == DEVICE_REPORT_UNKNOWN)
    return DEVICE_WAITING;
  if (s->device_report_age_ms > 2000)
    return DEVICE_STALE;
  return s->device_report == DEVICE_REPORT_RUN ? DEVICE_RUNNING
                                               : DEVICE_STOPPED;
}
const char *device_status_text(DeviceStatus state) {
  static const char *labels[] = {"设备未连接",     "设备连接中", "等待状态上报",
                                 "设备状态已过期", "设备运行中", "设备已停止"};
  return labels[state];
}
uint32_t device_status_color(DeviceStatus state) {
  if (state == DEVICE_RUNNING || state == DEVICE_CONNECTING)
    return 0x0874bd;
  if (state == DEVICE_STOPPED)
    return 0x138056;
  if (state == DEVICE_WAITING || state == DEVICE_STALE)
    return 0x926000;
  return 0x687480;
}
