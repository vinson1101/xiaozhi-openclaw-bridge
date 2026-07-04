# Phase 6Q WiFi Provisioning Validation

Phase 6Q attempts to provision the flashed board against a local Bridge and validate a real `/device/hello`.

## Result

The firmware provisioning write path works, and the board preserves the custom `xob` NVS namespace across non-erase firmware writes.

The initial failure was caused by a provisioned SSID typo, not by 2.4 GHz
visibility. The board scanned the expected 2.4 GHz network under the corrected
SSID spelling, then connected successfully after reprovisioning.

The real `/device/hello` is validated against a reachable VPS Bridge. The board
still cannot open a TCP connection to the Bridge process running on the Mac LAN
address because the current WiFi network blocks client-to-client reachability.

Sanitized serial evidence:

```text
WiFi scan: aps=12 target_len=11 target_matches=2 best_channel=4 best_rssi=-50 auth=3
WiFi connected
ping gateway: sent=3 received=3
ping internet: sent=3 received=3
ping bridge_host: sent=3 received=0
device hello failed: ESP_ERR_HTTP_CONNECT
```

The successful scan match and `WiFi connected` line prove the flashed board can
join the home WiFi after the SSID is corrected. The HTTP error occurs after IP
acquisition. Board-side ping diagnostics show that the board can reach the
router and internet, but cannot reach the configured Bridge host on the Mac.

## 2026-07-04 Reachable Host Validation

The remaining blocker was not firmware WiFi, Bridge binding, or macOS firewall.
The board and Mac were on the same subnet, but both directions failed ICMP/TCP
reachability. Moving Bridge to a VPS host that the board can reach resolved the
path.

Sanitized serial evidence:

```text
App version: c72aa5b
WiFi connected
ping gateway: sent=3 received=3
ping internet: sent=3 received=3
ping bridge_host: sent=3 received=3
device_token=configured
device hello status=200
Bridge hello complete
```

Sanitized Bridge evidence:

```text
GET /healthz -> 200
POST /command -> 404
POST /device/hello -> 200
```

The VPS Bridge was run with `--require-device-token` and
`--disable-command-route`. The board was provisioned with a random non-empty
device token over USB serial. The token was not printed or committed.

## 2026-07-04 Retest

The local Bridge was started on `0.0.0.0:8788`, and `/healthz` was reachable
from the Mac loopback path. A board reset still produced:

```text
WiFi connected
ping gateway: sent=3 received=3
ping internet: sent=3 received=3
ping bridge_host: sent=3 received=0
device hello failed: ESP_ERR_HTTP_CONNECT
```

This rules out a localhost-only Bridge bind as the remaining cause. The next
check is still network reachability to the configured Bridge host: verify the
Bridge URL points at the current reachable host, test from another WiFi client,
or move the Bridge to a host/VPS the board can reach.

## Network Check

Mac-side read-only diagnostics showed:

- the Mac was connected through WiFi and had a valid LAN IPv4 address
- the default gateway was reachable
- the local Bridge process was listening on port `8788`
- the Bridge port was reachable on both loopback and the Mac LAN address
- the board appeared in the Mac ARP table on the same `/24`
- ping from the Mac to the board failed
- ping from the board to the gateway succeeded
- ping from the board to the internet succeeded
- ping from the board to the configured Mac Bridge host failed
- no TCP connection from the board appeared on the Mac listener during a reboot
- the macOS application firewall was disabled

That makes the remaining issue local network reachability between WiFi clients
or local-network permission for inbound connections to the Mac process. It is
not currently a firmware WiFi association, DHCP, DNS, gateway, or internet
failure.

## Firmware Changes

- Use USB Serial/JTAG driver reads for provisioning input instead of `stdin`.
- Keep WiFi driver storage in RAM so station config is not persisted by the WiFi stack.
- Set WiFi country to CN so 2.4 GHz channels 1-13 are candidates.
- Log only aggregate scan diagnostics and target match counts, never secrets.
- Retry WiFi connection before failing.
- On WiFi failure, show a red WiFi status marker and return to USB serial provisioning instead of showing a squashed error eye.
- Match scanned SSIDs by exact length and bytes, then log only safe hashes and aggregate match data.
- Use all-channel station scan/connect settings so band steering and multiple APs are less likely to pick the wrong candidate.
- Add board-side network diagnostics for DHCP details plus gateway, internet, and Bridge-host ping.
- Allow serial and AP provisioning to keep existing non-empty `xob` values when
  a field is submitted empty. This lets Bridge URL changes avoid retyping the
  WiFi password.

## Next

Keep the Mac LAN path classified as blocked unless the router/client-isolation
setting changes. Continue device work against a reachable Bridge host.

Temporary AP provisioning is implemented separately in `PHASE6R_AP_PROVISIONING.md`.
The real board is now pointed at a Bridge host it can reach. Browser form
submission for AP setup remains a separate UI check; USB serial provisioning is
validated for updating only changed fields.
