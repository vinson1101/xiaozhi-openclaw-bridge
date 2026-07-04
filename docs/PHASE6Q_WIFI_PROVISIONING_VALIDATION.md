# Phase 6Q WiFi Provisioning Validation

Phase 6Q attempts to provision the flashed board against a local Bridge and validate a real `/device/hello`.

## Result

The firmware provisioning write path works, and the board preserves the custom `xob` NVS namespace across non-erase firmware writes.

The initial failure was caused by a provisioned SSID typo, not by 2.4 GHz
visibility. The board scanned the expected 2.4 GHz network under the corrected
SSID spelling, then connected successfully after reprovisioning.

The real `/device/hello` is not validated yet because the board cannot open a
TCP connection to the Bridge process running on the Mac LAN address.

Sanitized serial evidence:

```text
WiFi scan: aps=12 target_len=11 target_matches=2 best_channel=4 best_rssi=-50 auth=3
WiFi connected
device hello failed: ESP_ERR_HTTP_CONNECT
```

The successful scan match and `WiFi connected` line prove the flashed board can
join the home WiFi after the SSID is corrected. The HTTP error occurs after IP
acquisition.

## Network Check

Mac-side read-only diagnostics showed:

- the Mac was connected through WiFi and had a valid LAN IPv4 address
- the default gateway was reachable
- the local Bridge process was listening on port `8788`
- the Bridge port was reachable on both loopback and the Mac LAN address
- the board appeared in the Mac ARP table on the same `/24`
- ping from the Mac to the board failed
- no TCP connection from the board appeared on the Mac listener during a reboot
- the macOS application firewall was disabled

That makes the remaining issue local network reachability between WiFi clients
or local-network permission for inbound connections to the Mac process. It is
not currently a firmware WiFi association failure.

## Firmware Changes

- Use USB Serial/JTAG driver reads for provisioning input instead of `stdin`.
- Keep WiFi driver storage in RAM so station config is not persisted by the WiFi stack.
- Set WiFi country to CN so 2.4 GHz channels 1-13 are candidates.
- Log only aggregate scan diagnostics and target match counts, never secrets.
- Retry WiFi connection before failing.
- On WiFi failure, show a red WiFi status marker and return to USB serial provisioning instead of showing a squashed error eye.
- Match scanned SSIDs by exact length and bytes, then log only safe hashes and aggregate match data.
- Use all-channel station scan/connect settings so band steering and multiple APs are less likely to pick the wrong candidate.

## Next

Keep Phase 6Q open until a real `/device/hello` reaches the local Bridge.
Before changing firmware again, validate LAN reachability from another device
on the same WiFi by opening the Bridge health endpoint, or run the Bridge on a
host/VPS that the board can reach.

The next firmware feature should be temporary AP provisioning with a local HTTP form. That gives normal WiFi and Bridge configuration without USB serial tooling.
