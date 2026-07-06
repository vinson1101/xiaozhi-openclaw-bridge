# Provisioning And Configuration

The original Xiaozhi firmware has its own cloud-backed configuration flow. Custom firmware cannot rely on that data format.

## Rule

Treat stock configuration as foreign data. Preserve it when possible, but do not depend on it.

Custom firmware owns a separate NVS namespace:

```text
xob.bridge_url
xob.device_token
xob.wifi_ssid
xob.wifi_password
xob.default_target
xob.volume
xob.brightness
```

Secrets and WiFi values stay in NVS only. They must not be logged or committed.

## First Boot

Boot sequence:

1. Read `xob` NVS config.
2. If complete, connect WiFi and send `/device/hello`.
3. If missing, enter provisioning mode.

Provisioning mode should show a screen state and expose two setup paths:

- USB serial setup for development.
- Temporary WiFi AP plus local HTTP form for normal use.

The AP name is `XOB-<device-suffix>`. The temporary AP password is
`xob-<lowercase device-suffix>`. The local form writes only the `xob` namespace.

The implemented paths are USB serial provisioning and temporary AP provisioning.
Both accept `bridge_url`, `device_token`, `default_target`, `wifi_ssid`, and
`wifi_password`, then write only the `xob` namespace and reboot. If
`default_target` is missing or empty, firmware falls back to `fake`.
`default_target` is only a route name; the Bridge maps names such as `huntmind`,
`openclaw`, or `hermas` to the actual backend for the current LAN or VPS
environment.

The AP form treats `default_target` as a free text value rather than a fixed
WiFi choice. It is the agent/backend route, not a network SSID.

During development, a WiFi connection failure also falls back to the same
provisioning mode. This lets the operator correct SSID, password, Bridge URL, or
token without erasing flash or touching stock NVS data.

The physical buttons also enter provisioning: hold all three at app startup, or
hold volume down and volume up together for two seconds while the firmware is
running. The middle button may have hardware power behavior on long-press, so
runtime provisioning avoids holding it.

When the firmware is already in USB serial text command mode, typing `:config`
or `:setup` also enters the same AP plus serial provisioning flow. This is the
fallback when physical button mapping is still under test.

Typing `:target <name>` updates only `xob.default_target` and reboots. This is
the preferred development path for switching between `fake`, `huntmind`, and
future agent profiles without touching WiFi credentials or the device token.

The firmware keeps WiFi driver storage in RAM, then sets WiFi country to CN so 2.4 GHz channels 1-13 are scan/connect candidates. Diagnostic logs may report aggregate scan counts and target match counts, but must not print SSID values, WiFi passwords, device tokens, raw MAC addresses, or Bridge secrets.

The firmware must not call `nvs_flash_erase()` automatically. If NVS recovery is needed, stop and require an explicit restore or erase decision.

## Reset

Long-press reset should erase only the `xob` namespace, not the whole flash.

## Flash Safety

The firmware partition table mirrors the stock board layout:

| Name | Type/SubType | Offset | Size |
|---|---:|---:|---:|
| `nvs` | data/nvs | `0x009000` | `0x004000` |
| `otadata` | data/ota | `0x00d000` | `0x002000` |
| `phy_init` | data/phy | `0x00f000` | `0x001000` |
| `model` | data/0x82 | `0x010000` | `0x0f0000` |
| `ota_0` | app/ota_0 | `0x100000` | `0x380000` |
| `ota_1` | app/ota_1 | `0x480000` | `0x380000` |

Do not use a generic ESP-IDF partition template on this board.

First flash should follow the reviewed non-erase command in `docs/PHASE6G_FIRST_FLASH_REVIEW.md`. App-only flashing is not the default because stock `otadata` may not point at `ota_0`. Full-chip erase requires explicit restore review.
