/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/kernel.h>
typedef void zmk_event_t;
void studio_test_lock_event(int state);
#define ZMK_EVENT_DECLARE(name) int raise_##name(struct name event)
#define ZMK_EVENT_IMPL(name)                                                                       \
    int raise_##name(struct name event) {                                                          \
        studio_test_lock_event(event.state);                                                       \
        return 0;                                                                                  \
    }
#define ZMK_LISTENER(name, callback)
#define ZMK_SUBSCRIPTION(name, event)
