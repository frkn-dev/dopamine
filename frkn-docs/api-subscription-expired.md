# Subscription expiry — backend spec

How the backend should signal an **expired subscription** so the Dopamine
client can show a proper paywall instead of silently connecting with a stale
config or dying with a generic error.

## The problem being solved

At connect time the client refreshes the config (`POST /v1/config`) when its
cached copy is older than `expires_at`. Today:

- any failure of that refresh falls back to the installed config — so an
  expired user either keeps using the VPN for free (peer still alive) or sees
  a meaningless connection error (peer revoked);
- the client has no way to distinguish "API down" from "subscription expired".

## Contract — `POST /v1/config` on expired subscription

When the subscription attached to `auth_data.api_key` is **expired, revoked,
or disabled**, respond:

```
HTTP 402 Payment Required
```

with the body being the **AGW-encrypted** (same envelope as 200 responses —
the client always decrypts) JSON:

```json
{
  "error": "subscription_expired",
  "end_date": "2026-08-28T12:00:00Z",
  "message": "Subscription expired on 2026-08-28. Renew to continue."
}
```

| Field | Required | Notes |
|---|---|---|
| `error` | yes | Exactly `"subscription_expired"`. Other values are treated as generic errors. |
| `end_date` | yes | ISO 8601 UTC of when the subscription ended (empty string if unknown). |
| `message` | no | Human-readable; the client may display it (localized client-side otherwise). |

Rules:

- **Only 402** for this case. 4xx/5xx with any other code is treated as a
  generic backend error.
- The 402 body must be encrypted with the request's key/IV/salt like a normal
  response — unencrypted bodies fail decryption and surface as transport errors.
- `404` keeps its current meaning ("config not found" — e.g. device revoked).
- Expired **trial** accounts get the same 402 treatment.
- Server-side enforcement stays mandatory: the node must revoke/stop issuing
  peers when the subscription ends. 402 is only the UX layer.

## Recommended: same signal in `/v1/services`

For the import/reload flows, also return 402 with the same body from
`POST /v1/services` when the subscription is expired. The client then shows
the paywall on account import/reload too, instead of "no configs".

Optional (nice to have, not required for v1): `/v1/account_info` already
returns `subscription_end_date` — keep it accurate; the client displays it on
the server card («Active · until …»).

## Client behavior (for context)

- 402 at connect → no fallback to the installed config; user sees
  «Subscription expired» with a Renew button (opens the IAP paywall).
- Any other error at refresh → fallback to the installed config (API-down
  resilience, unchanged).
- 402 on import/reload → paywall instead of an empty server list.

## Checklist (backend)

- [ ] `/v1/config`: 402 + encrypted `subscription_expired` body when the
      subscription is expired/revoked/disabled
- [ ] Node revokes the peer at subscription end (enforcement, not just UX)
- [ ] Same 402 from `/v1/services` (recommended)
- [ ] `subscription_end_date` stays accurate in `/v1/account_info`
