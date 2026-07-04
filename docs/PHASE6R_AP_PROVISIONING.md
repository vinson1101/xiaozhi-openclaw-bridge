# Phase 6R AP Provisioning

Phase 6R adds the normal setup path for a flashed board when USB serial tooling is inconvenient.

## Result

When `xob` config is missing, or WiFi connection fails, the firmware now starts:

```text
SSID: XOB-<device-suffix>
Password: xob-<lowercase device-suffix>
URL: http://192.168.4.1/
```

The setup page accepts:

- `bridge_url`
- `device_token`
- `default_target`
- `wifi_ssid`
- `wifi_password`

`bridge_url` and `wifi_ssid` are required. `device_token`, `default_target`,
and `wifi_password` may be empty for local development or open WiFi. Empty
`default_target` falls back to `fake`.

On save, the firmware writes only the existing `xob` NVS namespace and reboots.
The AP server runs alongside the existing USB serial provisioning loop, so the
serial path remains the fallback.

After WiFi is connected and USB serial text command mode is active, typing
`:config` or `:setup` enters this same provisioning flow without relying on the
physical buttons.

## Boundaries

- No captive portal or DNS redirect.
- No token rotation or pairing admin UI.
- No stock NVS reads or erases.
- No WiFi password, device token, raw MAC, or real Bridge address is logged or committed.

## Validation

Validated in this phase:

```text
python3 scripts/check_firmware_skeleton.py
python3 scripts/check_eye_render.py
idf.py build
```

Real browser submission on the board remains pending until the next flash.

## Board Check

After the firmware was flashed, typing `:config` in USB serial text command mode
started AP provisioning and entered the serial provisioning prompt:

```text
AP provisioning started: ssid=XOB-<suffix> password=xob-<lowercase suffix> url=http://192.168.4.1/
XOB provisioning over USB serial
bridge_url:
```

Browser form submission remains pending, but the runtime AP entry path no longer
depends on the physical buttons.
