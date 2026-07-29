# Active Devices — backend spec

How the Dopamine client's **Active Devices** feature works: list devices on a
subscription and unlink (revoke) them. Derived from
`client/ui/models/api/apiDevicesModel.cpp`,
`client/ui/controllers/api/apiSettingsController.cpp`,
`client/ui/controllers/api/apiConfigsController.cpp`
(`deactivateDevice`, `deactivateExternalDevice`).

Transport for all endpoints: **AGW encrypted envelope** (see
`frkn-docs/api-news.md` — same `keyPayload`/`apiPayload` scheme, encrypted
response bodies).

## Data source — `POST /v1/account_info`

Already used by the client; the devices list is built from it.

### Request payload (decrypted)

```json
{
  "user_country_code": "CZ",
  "service_type": "amnezia-premium",
  "auth_data": { "api_key": "<subscription_id>", "id": "<subscription_id>" },
  "cli_version": "4.8.14.25",
  "app_language": "ru"
}
```

### Response payload (decrypted) — fields the devices UI uses

```json
{
  "active_device_count": 2,
  "max_device_count": 5,
  "subscription_end_date": "2026-08-28T12:00:00Z",
  "issued_configs": [
    {
      "installation_uuid": "c6831dad-....",
      "os_version": "ios",
      "source_type": "gateway_account",
      "last_downloaded": "2026-07-29T10:15:00Z",
      "worker_last_updated": "2026-07-29T10:20:00Z",
      "server_country_name": "CZ",
      "server_country_code": "CZ"
    }
  ]
}
```

| Field | Notes |
|---|---|
| `active_device_count` / `max_device_count` | Shown as «N out of M» («Active Connections» on the server card). |
| `issued_configs[].installation_uuid` | **Device identifier.** The client generates it per install and sends it on every `/v1/config` request as `installation_uuid`. |
| `issued_configs[].source_type` | The client shows only entries with `source_type == "gateway_account"` — other sources are filtered out. |
| `issued_configs[].os_version` | Displayed as the device platform (e.g. `ios`, `macos`, `android`, `windows`). |
| `issued_configs[].last_downloaded` | Last config fetch by this device — shown as «last update» date. |
| `issued_configs[].worker_last_updated` | When the node's config for this device last changed; if newer than `last_downloaded`, the client suggests reloading configs. |

**A config request from a device should upsert its `issued_configs` entry**
(installation_uuid + os_version + timestamps) — that is what makes a device
appear in the list.

## Unlink a device — `POST /v1/revoke_config`

### Request payload (decrypted)

Same envelope fields as `/v1/config` (GatewayRequestData):

```json
{
  "os_version": "ios",
  "app_version": "4.8.14.25",
  "app_language": "ru",
  "installation_uuid": "<uuid of the device to unlink>",
  "user_country_code": "CZ",
  "service_type": "amnezia-premium",
  "auth_data": { "api_key": "<subscription_id>", "id": "<subscription_id>" }
}
```

- `installation_uuid` — the device being revoked (can be the calling device
  itself or another device from the list).
- `auth_data` — the subscription credentials, same as for configs.

### Semantics

- Mark the issued config for this `installation_uuid` as revoked and **remove
  the peer/lease on the node(s)** so the device stops working until it
  re-fetches a config (the client offers «Reload API config» to reactivate
  itself).
- Remove (or tombstone) the entry from `issued_configs` so the next
  `/v1/account_info` no longer lists the device.
- `active_device_count` should drop accordingly.

### Response

- `200` — revoked (body may be empty; it is decrypted-and-ignored).
- `404` — config/device not found: the client tolerates this
  (`ApiNotFoundError` is treated as success — the device is gone anyway).
- Other errors → shown as a generic error popup.

## Client flow (for context)

1. Server card → «Active Devices» → client fetches `/v1/account_info` and
   renders devices (filtered by `source_type == "gateway_account"`).
2. The current device is marked by matching the local installation uuid.
3. «Unlink» on a device → `/v1/revoke_config` with its `installation_uuid`.
   If the user unlinks their *current* device, the client clears the local
   containers (server stops working locally until «Reload API config»).

## Notes / edge cases

- **No per-user auth**: everything is scoped by `auth_data` (subscription id) —
  anyone with the key can list/revoke devices. Acceptable for the current
  threat model (the key is the account), but do not log it in plaintext.
- Reinstalling the app generates a **new** installation uuid → a new device
  entry; the old one lingers until it is revoked or expires. A TTL for stale
  `issued_configs` (e.g. 90 days without `last_downloaded` refresh) keeps the
  list clean.
- If `max_device_count` is reached, `/v1/config` for a new device should fail
  with a clear error — the client shows it as-is.
