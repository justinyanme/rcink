#pragma once

#include "types.h"

bool ble_is_connected(void);

void init_ble(void);
int RxTxWrite(void *p);
void blt_pm_proc(void);
void ble_note_user_activity(void);

// When BLE_PM_PEEK_ENABLE is set, call once on deep-retention wake to
// re-arm advertising for BLE_PEEK_ADV_SECONDS. No-op when peek mode is
// disabled or the BLE sync window is still open.
void ble_open_peek_window(void);
void ble_open_sync_window(void);

// Refreshes the in-memory advertising buffer from STATUS.
void layout_refresh_adv_payload(void);
// Refreshes the advertising buffer and pushes it into the BLE stack.
void layout_apply_adv_payload(void);
// Updates battery/temp STATUS, applies advertising data, and notifies subscribers.
void update_adv_data(s16 temp, u16 battery_mv);
void set_airtag_adv_data(void);
