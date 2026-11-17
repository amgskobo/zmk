/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zmk/studio/core.h>

ZMK_EVENT_IMPL(zmk_studio_core_lock_state_changed);

static enum zmk_studio_core_lock_state state = IS_ENABLED(CONFIG_ZMK_STUDIO_LOCKING)
                                                   ? ZMK_STUDIO_CORE_LOCK_STATE_LOCKED
                                                   : ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
static struct k_spinlock state_guard;

enum zmk_studio_core_lock_state zmk_studio_core_get_lock_state(void) {
    k_spinlock_key_t key = k_spin_lock(&state_guard);
    enum zmk_studio_core_lock_state current = state;
    k_spin_unlock(&state_guard, key);
    return current;
}

static void notify_state(enum zmk_studio_core_lock_state new_state) {
    raise_zmk_studio_core_lock_state_changed(
        (struct zmk_studio_core_lock_state_changed){.state = new_state});
}

#if CONFIG_ZMK_STUDIO_LOCK_IDLE_TIMEOUT_SEC > 0

static int64_t lock_deadline;
static void core_idle_lock_timeout_cb(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(core_idle_lock_timeout, core_idle_lock_timeout_cb);

static void core_idle_lock_timeout_cb(struct k_work *work) {
    ARG_UNUSED(work);
    bool expired = false;
    k_spinlock_key_t key = k_spin_lock(&state_guard);
    if (state == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED) {
        int64_t remaining = lock_deadline - k_uptime_get();
        if (remaining > 0) {
            k_work_reschedule(&core_idle_lock_timeout, K_MSEC(remaining));
        } else {
            state = ZMK_STUDIO_CORE_LOCK_STATE_LOCKED;
            expired = true;
        }
    }
    k_spin_unlock(&state_guard, key);
    if (expired) {
        notify_state(ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
    }
}

#endif

void zmk_studio_core_reschedule_lock_timeout() {
#if CONFIG_ZMK_STUDIO_LOCK_IDLE_TIMEOUT_SEC > 0
    bool expired = false;
    k_spinlock_key_t key = k_spin_lock(&state_guard);
    if (state == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED) {
        int64_t now = k_uptime_get();
        if (lock_deadline <= now) {
            /* A delayed worker must not let late RPC revive an expired unlock. */
            state = ZMK_STUDIO_CORE_LOCK_STATE_LOCKED;
            k_work_cancel_delayable(&core_idle_lock_timeout);
            expired = true;
        } else {
            lock_deadline = now + (int64_t)CONFIG_ZMK_STUDIO_LOCK_IDLE_TIMEOUT_SEC * 1000;
            k_work_reschedule(&core_idle_lock_timeout,
                              K_SECONDS(CONFIG_ZMK_STUDIO_LOCK_IDLE_TIMEOUT_SEC));
        }
    }
    k_spin_unlock(&state_guard, key);
    if (expired) {
        notify_state(ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
    }
#endif
}

void zmk_studio_core_unlock() {
    k_spinlock_key_t key = k_spin_lock(&state_guard);
    bool changed = state != ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
    state = ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
#if CONFIG_ZMK_STUDIO_LOCK_IDLE_TIMEOUT_SEC > 0
    lock_deadline = k_uptime_get() + (int64_t)CONFIG_ZMK_STUDIO_LOCK_IDLE_TIMEOUT_SEC * 1000;
    k_work_reschedule(&core_idle_lock_timeout, K_SECONDS(CONFIG_ZMK_STUDIO_LOCK_IDLE_TIMEOUT_SEC));
#endif
    k_spin_unlock(&state_guard, key);
    if (changed) {
        notify_state(ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED);
    }
}

void zmk_studio_core_lock() {
    k_spinlock_key_t key = k_spin_lock(&state_guard);
    bool changed = state != ZMK_STUDIO_CORE_LOCK_STATE_LOCKED;
    state = ZMK_STUDIO_CORE_LOCK_STATE_LOCKED;
#if CONFIG_ZMK_STUDIO_LOCK_IDLE_TIMEOUT_SEC > 0
    k_work_cancel_delayable(&core_idle_lock_timeout);
#endif
    k_spin_unlock(&state_guard, key);
    if (changed) {
        notify_state(ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
    }
}
