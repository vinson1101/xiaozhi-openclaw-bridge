# Phase 6Q WiFi Provisioning Validation

Phase 6Q attempts to provision the flashed board against a local Bridge and validate a real `/device/hello`.

## Result

The firmware provisioning write path works, and the board preserves the custom `xob` NVS namespace across non-erase firmware writes.

The real `/device/hello` is not validated yet because the board cannot see the configured WiFi SSID on 2.4 GHz.

Sanitized serial evidence:

```text
WiFi scan: aps=7 target_matches=0
WiFi disconnected, retrying (1/8), reason=201
WiFi connection failed, reason=201
XOB provisioning over USB serial
```

`reason=201` is `WIFI_REASON_NO_AP_FOUND`. This is not a password failure.

## Network Check

Mac-side read-only diagnostics showed:

- the Mac was connected through WiFi and had a valid LAN IPv4 address
- the default gateway was reachable
- the local Bridge process was listening on port `8788`
- the Bridge port was reachable on both loopback and the Mac LAN address
- the Mac's current WiFi association was on 5 GHz

That rules out local routing as the cause of `NO_AP_FOUND`. The remaining issue is 2.4 GHz SSID visibility from the ESP32-C3: the 2.4 GHz radio may be disabled, hidden, on a different SSID, affected by band steering, or otherwise not visible from the board's location.

## Firmware Changes

- Use USB Serial/JTAG driver reads for provisioning input instead of `stdin`.
- Keep WiFi driver storage in RAM so station config is not persisted by the WiFi stack.
- Set WiFi country to CN so 2.4 GHz channels 1-13 are candidates.
- Log only aggregate scan diagnostics and target match counts, never secrets.
- Retry WiFi connection before failing.
- On WiFi failure, show a red WiFi status marker and return to USB serial provisioning instead of showing a squashed error eye.

## Next

Keep Phase 6Q open until a real `/device/hello` reaches the local Bridge.

The next firmware feature should be temporary AP provisioning with a local HTTP form. That gives normal WiFi and Bridge configuration without USB serial tooling.
