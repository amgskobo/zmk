/* SPDX-License-Identifier: MIT */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>
#include <zmk/ble.h>
#include <zmk/studio/ble_discovery.h>

LOG_MODULE_DECLARE(zmk_studio, CONFIG_ZMK_STUDIO_LOG_LEVEL);

/* Only this work item applies the Studio advertising request. Callbacks only
 * update bounded state and queue work; they never issue advertising commands. */
static struct k_spinlock discovery_lock;
static bool requested;
static int64_t deadline;
static int profile;
static uint32_t generation;
static uint8_t retries;

static void discovery_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(discovery_work, discovery_work_cb);

static void discovery_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    bool session = zmk_studio_ble_has_session();
    bool active = zmk_studio_ble_transport_is_active();
    int selected = zmk_ble_active_profile_index();
    k_spinlock_key_t key = k_spin_lock(&discovery_lock);
    int64_t remaining = deadline - k_uptime_get();
    if (requested && (remaining <= 0 || profile != selected || session || !active)) {
        requested = false;
        generation++;
        retries = 0;
    }
    bool want = requested;
    uint32_t observed = generation;
    k_spin_unlock(&discovery_lock, key);

    int err = zmk_ble_set_directed_advertising(want);
    if (err) {
        LOG_WRN("Studio discovery advertising request failed (%d)", err);
    }

    key = k_spin_lock(&discovery_lock);
    remaining = deadline - k_uptime_get();
    if (generation != observed || (requested && remaining <= 0)) {
        k_work_reschedule(&discovery_work, K_NO_WAIT);
    } else if (err && retries < 5) {
        retries++;
        k_work_reschedule(&discovery_work, K_MSEC(requested ? MIN(remaining, 1000) : 1000));
    } else if (requested) {
        k_work_reschedule(&discovery_work, K_MSEC(remaining));
    }
    k_spin_unlock(&discovery_lock, key);
}

void zmk_studio_ble_discovery_start(void) {
    int selected = zmk_ble_active_profile_index();
    k_spinlock_key_t key = k_spin_lock(&discovery_lock);
    requested = true;
    deadline = k_uptime_get() + (int64_t)CONFIG_ZMK_STUDIO_BLE_ADVERTISING_TIMEOUT_SEC * 1000;
    profile = selected;
    generation++;
    retries = 0;
    k_work_reschedule(&discovery_work, K_NO_WAIT);
    k_spin_unlock(&discovery_lock, key);
}

void zmk_studio_ble_discovery_stop(void) {
    k_spinlock_key_t key = k_spin_lock(&discovery_lock);
    if (!requested) {
        k_spin_unlock(&discovery_lock, key);
        return;
    }
    requested = false;
    generation++;
    retries = 0;
    k_work_reschedule(&discovery_work, K_NO_WAIT);
    k_spin_unlock(&discovery_lock, key);
}

void zmk_studio_ble_discovery_refresh(void) {
    k_spinlock_key_t key = k_spin_lock(&discovery_lock);
    if (requested) {
        retries = 0;
        k_work_reschedule(&discovery_work, K_NO_WAIT);
    }
    k_spin_unlock(&discovery_lock, key);
}

BT_CONN_CB_DEFINE(studio_discovery_conn_cb) = {.recycled = zmk_studio_ble_discovery_refresh};
