---
title: ZMK Studio Configuration
sidebar_label: ZMK Studio
---

The following settings affect the ZMK Studio portions of ZMK. See the [ZMK Studio feature](../features/studio.md) for more information on enabling and building with ZMK Studio enabled.

See [Configuration Overview](index.md) for instructions on how to change these settings.

## Kconfig

Definition file: [zmk/app/src/studio/Kconfig](https://github.com/zmkfirmware/zmk/blob/main/app/src/studio/Kconfig)

### Keymaps

| Config                                 | Type | Description                             | Default |
| -------------------------------------- | ---- | --------------------------------------- | ------- |
| `CONFIG_ZMK_KEYMAP_LAYER_NAME_MAX_LEN` | int  | Max allowable keymap layer display name | 20      |

### Locking

| Config                                    | Type | Description                                                                                                           | Default |
| ----------------------------------------- | ---- | --------------------------------------------------------------------------------------------------------------------- | ------- |
| `CONFIG_ZMK_STUDIO_LOCKING`               | bool | Enable/disable locking for ZMK Studio                                                                                 | y       |
| `CONFIG_ZMK_STUDIO_LOCK_IDLE_TIMEOUT_SEC` | int  | Seconds before relocking; decoded RPC renews the deadline only while unlocked. Range 0–86400; 0 disables the timeout. | 600     |
| `CONFIG_ZMK_STUDIO_LOCK_ON_DISCONNECT`    | bool | Whether to automatically lock again whenever ZMK Studio disconnects from the device                                   | y       |

### BLE discovery

| Config                                                    | Type | Description                                                                                  | Default                |
| --------------------------------------------------------- | ---- | -------------------------------------------------------------------------------------------- | ---------------------- |
| `CONFIG_ZMK_STUDIO_LOCK_BLE_DIRECT_ADVERTISING_ON_UNLOCK` | bool | Physical unlock behavior also starts an independent discovery window; requires BLE transport | y with locking and BLE |
| `CONFIG_ZMK_STUDIO_BLE_ADVERTISING_TIMEOUT_SEC`           | int  | Maximum discovery duration in seconds, range 1–3600                                          | 90                     |

The lock API changes permissions only. The physical `&studio_unlock` behavior
also starts discovery; callers can independently start or stop discovery through
`zmk_studio_ble_discovery_start()` and `zmk_studio_ble_discovery_stop()` from
`<zmk/studio/ble_discovery.h>`. The historical "directed" option name is retained;
the current backend uses ordinary named connectable advertising.

Only the selected BLE transport host's Studio indication subscription, or its
subscribed RPC write, consumes the discovery request. An HID connection alone
does not. A retained active subscription is checked before starting advertising.
RPC activity extends only the unlocked idle timeout. Lock changes, unsubscribe
and disconnect do not restart discovery. A new physical operation starts a new
window. Output or target-profile changes cancel the old request. Other owners'
HID/Messenger advertising conditions remain independent; ending Studio discovery
does not guarantee radio silence.

For example, a keyboard can select a 180-second RPC idle timeout and a separate
90-second discovery window. These are build settings, with no new persisted
settings or migration. Discovery adds fixed state, one delayable system work
item and a connection-recycle callback, with no additional thread or allocation.
Failed advertising requests receive at most five retries at one-second intervals
per event, bounded by the start deadline; stopping can also fail in the controller
and is reported after bounded retries. A recycle event retries a pending request.
Measure RAM/FLASH in the actual production map and verify browser discovery,
HID/split/Messenger coexistence, reconnect, settings/reset and power using the
installed firmware. Native tests cover request ordering, not physical BLE timing.

### Transport/Protocol Details

| Config                                                             | Type | Description                                                                   | Default                             |
| ------------------------------------------------------------------ | ---- | ----------------------------------------------------------------------------- | ----------------------------------- |
| `CONFIG_ZMK_STUDIO_TRANSPORT_BLE_PREF_LATENCY`                     | int  | Lower latency to request while ZMK Studio is active to improve responsiveness | 10                                  |
| `CONFIG_ZMK_STUDIO_RPC_THREAD_STACK_SIZE`                          | int  | Stack size for the dedicated RPC thread                                       | 1800                                |
| `CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE`                                | int  | Number of bytes available for buffering incoming messages                     | 30                                  |
| `CONFIG_ZMK_STUDIO_RPC_TX_BUF_SIZE`                                | int  | Number of bytes available for buffering outgoing messages                     | 64                                  |
| `CONFIG_ZMK_STUDIO_RPC_CUSTOM_SUBSYSTEM_REQUEST_PAYLOAD_MAX_BYTES` | int  | Max bytes for custom subsystem request payloads                               | `CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE` |
