#pragma once
#include "projects.h"
typedef enum {
  TRANSFER_IDLE,
  TRANSFER_PREPARING,
  TRANSFER_COMPILING,
  TRANSFER_UPLOADING,
  TRANSFER_SENT,
  TRANSFER_STOPPING,
  TRANSFER_STOPPED,
  TRANSFER_ERROR
} TransferState;
typedef struct {
  TransferState state;
  unsigned done, total;
  int error;
} TransferSnapshot;
bool ble_program_run(const Node *nodes);
bool ble_program_stop(void);
void ble_program_snapshot(TransferSnapshot *out);
