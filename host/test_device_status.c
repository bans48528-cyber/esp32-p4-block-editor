#include "device_status.h"
#include "spark_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  BleScanSnapshot s = {0};
  assert(device_status_view(&s) == DEVICE_DISCONNECTED);
  s.link = BLE_LINK_CONNECTING;
  assert(device_status_view(&s) == DEVICE_CONNECTING);
  s.link = BLE_LINK_READY;
  assert(device_status_view(&s) == DEVICE_WAITING);
  SparkRx rx = {0};
  const char *json = "{\"ports\":{\"A\":123},\"WillAiState\":\"stop\"}";
  int result = 0;
  for (size_t i = 0; i < strlen(json); i++) {
    result = spark_rx_byte(&rx, json[i]);
    assert(result == (i + 1 == strlen(json) ? 2 : 0));
  }
  s.device_report = device_report_parse(rx.json);
  assert(s.device_report == DEVICE_REPORT_STOP);
  assert(device_status_view(&s) == DEVICE_STOPPED);
  assert(!strcmp(device_status_text(DEVICE_STOPPED), "设备已停止"));
  assert(device_status_color(DEVICE_STOPPED) == 0x138056);
  s.device_report_age_ms = 2000;
  assert(device_status_view(&s) == DEVICE_STOPPED);
  s.device_report_age_ms = 2001;
  assert(device_status_view(&s) == DEVICE_STALE);
  s.device_report = device_report_parse("{\"WillAiState\":\"run\"}");
  s.device_report_age_ms = 0;
  assert(device_status_view(&s) == DEVICE_RUNNING);
  const char *bad[] = {"{}",
                       "{\"WillAiState\":false}",
                       "{\"WillAiState\":\"STOP\"}",
                       "{\"WillAiState\":\"stop\",}",
                       "{\"nested\":{\"WillAiState\":\"stop\"}}",
                       "{\"WillAiState\":\"run\"}garbage"};
  for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
    assert(device_report_parse(bad[i]) == DEVICE_REPORT_UNKNOWN);
  char deep[160];
  memset(deep, '[', 32);
  memset(deep + 32, ']', 32);
  deep[64] = 0;
  assert(device_report_parse(deep) == DEVICE_REPORT_UNKNOWN);
  assert(device_report_parse("{\"WillAiState\":\"stop\\u0000run\"}") ==
         DEVICE_REPORT_UNKNOWN);
  s.link = BLE_LINK_OFF;
  assert(device_status_view(&s) == DEVICE_DISCONNECTED);
  /* JSON never increments binary ACKs, including an oversize status. */
  unsigned acks = 0;
  const char *reports = "{\"WillAiState\":\"run\"}{\"WillAiState\":\"stop\"}";
  for (size_t i = 0; i < strlen(reports); i++)
    acks += spark_rx_byte(&rx, reports[i]) == 1;
  assert(acks == 0);
  assert(device_report_parse(rx.json) == DEVICE_REPORT_STOP);
  assert(spark_rx_byte(&rx, '{') == 0);
  for (int i = 0; i < 5000; i++)
    assert(spark_rx_byte(&rx, ' ') == 0);
  assert(spark_rx_byte(&rx, '}') == -1);
  uint8_t packet[262], one = 1;
  size_t n = spark_frame(packet, 0xda, &one, 1);
  for (size_t i = 0; i < n; i++)
    acks += spark_rx_byte(&rx, packet[i]) == 1;
  assert(acks == 1);
  puts("PASS: device reports, six display states, 2s expiry, malformed/missing "
       "status, fragmented/merged JSON and ACK isolation");
  return 0;
}
