# Phase 4B Device Pairing

Phase 4B adds durable device pairing to the HTTP JSON device protocol.

## Scope

- Add SQLite `device_pairings`.
- Pair a device on first `POST /device/hello`.
- Store device metadata:
  - `device_id`
  - `name`
  - token hash
  - firmware
  - capabilities
  - created time
  - last seen time
- Require the same Bearer token on future device requests when a token was used for pairing.

## HTTP Rule

Development without a token is allowed for local simulator work. For real devices, provision a token and send:

```text
Authorization: Bearer <device_token>
```

The Bridge stores only:

```text
sha256:<hash>
```

It does not store the raw token in SQLite or session events.

Deployment can enforce tokened pairing with:

```text
--require-device-token
```

When this flag is set, `POST /device/hello` without a Bearer token returns
`401 device token is required`.

## Validation

```bash
python3 scripts/smoke_device_http.py
```

The smoke test verifies:

- first hello creates a pairing row,
- require-token mode rejects hello without a token,
- the pairing stores a token hash, not the raw token,
- command without the token is rejected,
- command with the token still routes through the fake backend,
- the session event order remains stable.

Expected output:

```text
smoke_device_http ok
```

## Deferred

- Token rotation.
- Explicit admin pairing API.
- TLS and reverse proxy deployment.
- WebSocket device protocol.
