/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_ZMK_STUDIO_LOCK_BLE_DIRECT_ADVERTISING_ON_UNLOCK)
/* Discovery has its own deadline. Neither operation changes Studio permissions. */
void zmk_studio_ble_discovery_start(void);
void zmk_studio_ble_discovery_stop(void);
void zmk_studio_ble_discovery_refresh(void);

/* True only for the selected BLE transport's host with RPC indications enabled. */
bool zmk_studio_ble_has_session(void);
bool zmk_studio_ble_transport_is_active(void);
#else
static inline void zmk_studio_ble_discovery_start(void) {}
static inline void zmk_studio_ble_discovery_stop(void) {}
static inline void zmk_studio_ble_discovery_refresh(void) {}
#endif
