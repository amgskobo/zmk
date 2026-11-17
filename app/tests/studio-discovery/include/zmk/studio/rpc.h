/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/sys/ring_buffer.h>
#include <zmk/studio/core.h>
struct ring_buf *zmk_rpc_get_rx_buf(void);
struct ring_buf *zmk_rpc_get_tx_buf(void);
void zmk_rpc_rx_notify(void);
#define ZMK_RPC_TRANSPORT(name, transport, start, stop, user_data, notify)
