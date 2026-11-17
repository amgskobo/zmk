/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/bluetooth/conn.h>
int zmk_ble_active_profile_index(void);
struct bt_conn *zmk_ble_active_profile_conn(void);
int zmk_ble_set_directed_advertising(bool enable);
