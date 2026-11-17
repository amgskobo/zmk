/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/studio/core.h>
#include <zmk/studio/ble_discovery.h>

/* Compile the actual transport against real Zephyr types. Only hardware/peer
 * observations are substituted; scenarios and assertions stay in this owner. */
#include "../../src/studio/gatt_rpc_transport.c"

RING_BUF_DECLARE(test_rx, 128);
RING_BUF_DECLARE(test_tx, 128);
static int selected_profile;
static int selected_storage;
static int foreign_storage;
static struct bt_conn *selected_conn;
static bool subscribed;
static bool advertising;
static int advertising_calls;
static int inject_failures;
static bool cancel_during_apply;
static int rx_notifications;
static unsigned cases;
static K_SEM_DEFINE(queue_blocked, 0, 1);
static K_SEM_DEFINE(queue_release, 0, 1);
static void hold_queue(struct k_work *work) {
    k_sem_give(&queue_blocked);
    k_sem_take(&queue_release, K_FOREVER);
}
static K_WORK_DEFINE(queue_blocker, hold_queue);

#define HOST ((struct bt_conn *)&selected_storage)
#define FOREIGN ((struct bt_conn *)&foreign_storage)
#define CHECK(condition, message) __ASSERT(condition, "%s", message)

static void settle(void) { k_sleep(K_MSEC(10)); }
static void advance(int ms) { k_sleep(K_MSEC(ms)); }
static void passed(const char *name) {
    cases++;
    printk("STUDIO_CASE_PASS %s t=%lld\n", name, (long long)k_uptime_get());
}

void studio_test_lock_event(int state) {
    printk("STUDIO_LOCK state=%d t=%lld\n", state, (long long)k_uptime_get());
}
int zmk_ble_active_profile_index(void) { return selected_profile; }
struct bt_conn *zmk_ble_active_profile_conn(void) { return selected_conn; }
int zmk_ble_set_directed_advertising(bool enable) {
    advertising_calls++;
    printk("STUDIO_ADV want=%d t=%lld\n", enable, (long long)k_uptime_get());
    if (cancel_during_apply) {
        cancel_during_apply = false;
        zmk_studio_ble_discovery_stop();
    }
    if (inject_failures) {
        inject_failures--;
        return -ENOMEM;
    }
    advertising = enable;
    return 0;
}
void bt_conn_unref(struct bt_conn *conn) {
    CHECK(conn == HOST, "balanced selected-host reference");
}
int bt_conn_get_info(const struct bt_conn *conn, struct bt_conn_info *info) {
    static struct bt_conn_le_data_len_info data_len = {.tx_max_len = 251};
    memset(info, 0, sizeof(*info));
    info->state = BT_CONN_STATE_CONNECTED;
    info->role = BT_CONN_ROLE_PERIPHERAL;
    info->le.data_len = &data_len;
    return 0;
}
bool bt_gatt_is_subscribed(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           uint16_t ccc_type) {
    CHECK(attr == &rpc_interface.attrs[1], "query actual RPC characteristic");
    CHECK(ccc_type == BT_GATT_CCC_INDICATE, "require indications");
    return conn == HOST && subscribed;
}
int bt_gatt_indicate(struct bt_conn *conn, struct bt_gatt_indicate_params *params) { return 0; }
/* Attribute metadata is linked with the production service. These stack I/O
 * adapters are not exercised; peer observations and CCC callbacks are explicit. */
ssize_t bt_gatt_attr_read_service(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                                  uint16_t len, uint16_t offset) {
    return 0;
}
ssize_t bt_gatt_attr_read_chrc(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                               uint16_t len, uint16_t offset) {
    return 0;
}
ssize_t bt_gatt_attr_read_ccc(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset) {
    return 0;
}
ssize_t bt_gatt_attr_write_ccc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    return 0;
}
struct ring_buf *zmk_rpc_get_rx_buf(void) { return &test_rx; }
struct ring_buf *zmk_rpc_get_tx_buf(void) { return &test_tx; }
void zmk_rpc_rx_notify(void) { rx_notifications++; }

static void reset_observations(void) {
    zmk_studio_ble_discovery_stop();
    settle();
    zmk_studio_core_lock();
    selected_conn = NULL;
    subscribed = false;
    selected_profile = 0;
    gatt_stop_rx();
    gatt_start_rx();
    advertising_calls = 0;
    CHECK(!advertising, "previous advertising request ended");
}

#if IS_ENABLED(CONFIG_ZMK_STUDIO_LOCK_BLE_DIRECT_ADVERTISING_ON_UNLOCK) &&                         \
    CONFIG_ZMK_STUDIO_LOCK_IDLE_TIMEOUT_SEC > 0
int main(void) {
    reset_observations();
    zmk_studio_core_reschedule_lock_timeout();
    zmk_studio_core_unlock();
    settle();
    CHECK(advertising_calls == 0, "REGRESSION: unlock must not directly request advertising");
    CHECK(zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED, "unlock");
    zmk_studio_ble_discovery_start();
    settle();
    CHECK(advertising, "discovery independently starts");
    advance(60000);
    zmk_studio_core_reschedule_lock_timeout();
    advance(30010);
    CHECK(!advertising, "90-second deadline is independent of RPC idle renewal");
    CHECK(zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED,
          "RPC renewed lock");
    advance(149970);
    CHECK(zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED,
          "before renewed deadline");
    advance(30);
    CHECK(zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED,
          "180 seconds after renewal");
    passed("independent-90s-discovery-180s-rpc-idle");

    reset_observations();
    selected_conn = HOST;
    gatt_start_rx();
    zmk_studio_core_unlock();
    zmk_studio_ble_discovery_start();
    settle();
    CHECK(advertising, "HID connection alone does not end discovery");
    rpc_ccc_write(FOREIGN, NULL, BT_GATT_CCC_INDICATE);
    settle();
    CHECK(advertising, "foreign subscription cannot end active host discovery");
    rpc_ccc_write(HOST, NULL, BT_GATT_CCC_INDICATE);
    subscribed = true;
    settle();
    CHECK(!advertising, "active Studio subscription ends discovery");
    CHECK(zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED,
          "subscription preserves unlock");
    passed("selected-host-subscription-not-hid-or-foreign");

    uint8_t request[] = {1, 2, 3};
    int before_rx = rx_notifications;
    CHECK(write_rpc_req(FOREIGN, NULL, request, sizeof(request), 0, 0) ==
              BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION),
          "reject foreign RPC");
    subscribed = false;
    CHECK(write_rpc_req(HOST, NULL, request, sizeof(request), 0, 0) ==
              BT_GATT_ERR(BT_ATT_ERR_CCC_IMPROPER_CONF),
          "reject unsubscribed RPC");
    CHECK(rx_notifications == before_rx, "rejected writes do not reach RPC decoder");
    subscribed = true;
    CHECK(write_rpc_req(HOST, NULL, request, sizeof(request), 0, 0) == sizeof(request),
          "active RPC accepted");
    CHECK(rx_notifications == before_rx + 1, "accepted RPC reaches decoder");
    CHECK(ring_buf_size_get(&test_rx) == sizeof(request), "exact accepted request bytes");
    ring_buf_reset(&test_rx);
    int calls = advertising_calls;
    for (int i = 0; i < 3; i++) {
        zmk_studio_core_reschedule_lock_timeout();
        CHECK(write_rpc_req(HOST, NULL, request, sizeof(request), 0, 0) == sizeof(request),
              "ongoing RPC");
        ring_buf_reset(&test_rx);
    }
    settle();
    CHECK(advertising_calls == calls,
          "RPC neither restarts discovery nor creates idle advertising work");
    passed("rpc-peer-ccc-gate-and-no-readvertising");

    zmk_studio_core_unlock();
    zmk_studio_ble_discovery_start();
    settle();
    CHECK(!advertising, "existing active subscription needs no discovery");
    rpc_ccc_write(HOST, NULL, 0);
    subscribed = false;
    settle();
    CHECK(!advertising, "unsubscribe does not reopen consumed window");
    selected_conn = NULL;
    gatt_rpc_listener(NULL);
    settle();
    CHECK(!advertising, "disconnect does not reopen consumed window");
    CHECK(zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED,
          "existing disconnect lock preserved");
    passed("retained-ccc-unsubscribe-disconnect");

    reset_observations();
    zmk_studio_ble_discovery_start();
    settle();
    CHECK(advertising, "discovery while locked");
    zmk_studio_core_unlock();
    zmk_studio_core_lock();
    settle();
    CHECK(advertising, "lock transitions do not cancel discovery");
    advance(45000);
    zmk_studio_ble_discovery_start();
    settle();
    advance(45010);
    CHECK(advertising, "new physical discovery replaces old deadline");
    advance(45000);
    CHECK(!advertising, "replacement expires at its own 90-second deadline");
    passed("locked-discovery-and-physical-restart");

    reset_observations();
    zmk_studio_ble_discovery_start();
    settle();
    selected_profile = 1;
    gatt_rpc_listener(NULL);
    settle();
    CHECK(!advertising, "profile change cancels old discovery identity");
    passed("profile-replacement");

    reset_observations();
    inject_failures = 2;
    zmk_studio_ble_discovery_start();
    settle();
    CHECK(!advertising, "failed start is not a successful advertisement");
    advance(2010);
    CHECK(advertising, "bounded retry recovers");
    zmk_studio_ble_discovery_stop();
    settle();
    CHECK(!advertising, "explicit stop");
    zmk_studio_ble_discovery_start();
    settle();
    inject_failures = 2;
    zmk_studio_ble_discovery_stop();
    settle();
    CHECK(advertising, "failed stop preserves observed active advertisement");
    advance(2010);
    CHECK(!advertising, "failed stop retries without reopening admission");
    passed("failure-recovery");

    reset_observations();
    inject_failures = 20;
    zmk_studio_ble_discovery_start();
    advance(6010);
    CHECK(advertising_calls == 6, "one initial attempt and five retries");
    inject_failures = 0;
    zmk_studio_ble_discovery_refresh();
    settle();
    CHECK(advertising, "recycled connection event retries pending request");
    zmk_studio_ble_discovery_stop();
    settle();
    CHECK(!advertising, "end recovered request");
    passed("bounded-retries-and-resource-event");

    reset_observations();
    cancel_during_apply = true;
    zmk_studio_ble_discovery_start();
    settle();
    CHECK(!advertising, "stop during apply wins over stale start snapshot");
    passed("ordered-cancel-during-apply");

    zmk_studio_core_reschedule_lock_timeout();
    advance(180010);
    CHECK(zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED,
          "locked RPC cannot unlock");
    CHECK(!advertising, "idle remains quiet");
    passed("locked-rpc-no-unlock");
    zmk_studio_core_unlock();
    k_work_submit(&queue_blocker);
    k_sem_take(&queue_blocked, K_FOREVER);
    advance(180010);
    zmk_studio_core_reschedule_lock_timeout();
    CHECK(zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED,
          "late RPC cannot revive unlock while the timeout worker is blocked");
    k_sem_give(&queue_release);
    settle();
    passed("expired-rpc-with-delayed-workqueue");
    reset_observations();
    gatt_stop_rx();
    zmk_studio_ble_discovery_start();
    settle();
    CHECK(!advertising, "USB transport does not request BLE discovery");
    gatt_start_rx();
    settle();
    CHECK(!advertising, "switching transport cannot revive consumed request");
    zmk_studio_ble_discovery_start();
    settle();
    CHECK(advertising, "explicit BLE discovery still works");
    gatt_stop_rx();
    settle();
    CHECK(!advertising, "leaving BLE transport cancels discovery");
    passed("usb-ble-transport-boundary");
    printk("STUDIO_DISCOVERY_PASS cases=%u\n", cases);
    exit(0);
}
#else
int main(void) {
    reset_observations();
    zmk_studio_core_unlock();
    settle();
    CHECK(advertising_calls == 0, "unlock remains independent in optional configurations");
    zmk_studio_ble_discovery_start();
    settle();
#if IS_ENABLED(CONFIG_ZMK_STUDIO_LOCK_BLE_DIRECT_ADVERTISING_ON_UNLOCK)
    CHECK(advertising, "discovery with idle timeout disabled");
    advance(90010);
    CHECK(!advertising, "discovery retains finite deadline with unlimited unlock");
    advance(180010);
    CHECK(zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED,
          "zero timeout explicitly disables idle relock");
#else
    CHECK(!advertising && advertising_calls == 0, "feature-off discovery calls are no-ops");
    advance(180010);
    CHECK(zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED,
          "idle lock still works without discovery");
#endif
    zmk_studio_core_lock();
    zmk_studio_core_reschedule_lock_timeout();
    CHECK(zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED,
          "optional configurations preserve explicit lock");
    passed("configuration-boundary");
    printk("STUDIO_CONFIG_PASS cases=1\n");
    exit(0);
}
#endif
