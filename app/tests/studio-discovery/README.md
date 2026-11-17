# Studio lock and BLE discovery runtime checks

Run in the digest-selected prepared Docker environment, from the core owner:

```sh
python3 app/tests/studio-discovery/run.py \
  --zephyr /prepared/workspace/zephyr \
  --output /results/FRESH_DIRECTORY \
  --original-core e5c9b6915b56801193e359dd9bad4a167ce0d1b8
```

The output parent must exist on a retained results mount. The runner refuses
existing destinations, including symlinks. It records commands, exits, source and
compiled-input hashes, config/DTS, binaries, ordered request/lock times and source
drift. It builds once per variant and runs that exact executable with `-no-rt`;
native simulated time advances without real-time pacing.

The fixture compiles the actual lock core and discovery worker, and includes the
actual GATT transport with real selected Zephyr types, workqueue, clock and ring
buffers. Only event publication and peer/HCI observations are substituted. Its
attribute I/O adapters link the real service metadata but do not implement the
Bluetooth stack. It is not a radio, browser, full RPC decoder or installed-image
acceptance test. RPC idle-renewal entry is exercised directly; transport writes
are checked through delivery to the decoder input buffer.

Twelve required completions cover independent 90/180-second deadlines and RPC
renewal, HID-only/foreign/selected subscriptions, accepted and rejected writes,
retained CCC and unsubscribe/disconnect, lock independence and physical restart,
profile replacement, start failure recovery and bounded retries/resource events,
cancel during an in-progress advertising apply, locked RPC and an expired unlock
with its workqueue deliberately held, and the USB/BLE transport boundary. Failed
stops retain the observed active advertisement until a successful retry. The
original source must compile and then
fail the specific `REGRESSION: unlock must not directly request advertising`
assertion. A build error or any different assertion is not a qualifying control.

All variants use strict ASan/UBSan. The fixed variant must exit zero, have all
twelve case markers and the final completion, and contain no sanitizer, assertion
or kernel fatal diagnostic. Original-control rejection is reported separately.
Separate feature-off and timeout-zero builds require their own configuration
completion, and verify respectively that lock timeout remains active and that
discovery still expires while automatic relock is deliberately disabled.

Remaining gates: real concurrent
Bluetooth callbacks and peer replacement, the full RPC parser, logging transport,
all affected production keyboards/roles, hardware USB/BLE/HID/split/Messenger,
browser reconnect, settings/reset and power. Native observations do not establish
controller stop success or physical 90-second radio silence when another owner
still requests advertising.
