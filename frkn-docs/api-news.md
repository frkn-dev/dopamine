# News API — `POST /v1/news`

Backend spec for the News & Notifications feature in the Dopamine client.
Derived from the client code (`client/ui/controllers/api/apiNewsController.cpp`,
`client/ui/models/newsModel.cpp`, `client/core/controllers/gatewayController.cpp`).

## Endpoint

```
POST https://api.frkn.org/v1/news
Content-Type: application/json
X-Client-Request-ID: <uuid4>   # informational, client sends it on every AGW request
```

No auth token. The request is anonymous; targeting is done via the payload fields.
Client timeout: **12 s** (`apiDefs::requestTimeoutMsecs`).

## Transport — AGW encrypted envelope

Same envelope as the other `/v1/*` endpoints (`/v1/account_info`, etc.).
If your gateway already terminates that envelope, the news handler just plugs
into the same layer — this section is for reference.

Request body (outer JSON):

```json
{
  "keyPayload": "<base64>",
  "apiPayload": "<base64>"
}
```

- `apiPayload` = base64 of the AES-256-CBC encrypted **request payload JSON**
  (OpenSSL-compatible: EVP_BytesToKey KDF, random 32-byte key, 32-byte IV, 8-byte salt).
- `keyPayload` = base64 of RSA (PKCS#1 v1.5 padding) encryption, with the AGW
  public key shipped in the client (`agw_public.pem`), of a JSON:

```json
{
  "aes_key":  "<base64, 32 bytes>",
  "aes_iv":   "<base64, 32 bytes>",
  "aes_salt": "<base64, 8 bytes>"
}
```

**Response body** is NOT a JSON envelope: it is the base64 AES ciphertext of the
response JSON, encrypted with the **same key/IV/salt from the request**.
The client always tries to decrypt the body; an unencrypted body is treated as a
decryption error (so even error responses should be encrypted if you want the
client to handle them gracefully).

## Request payload (decrypted)

```json
{
  "locale": "ru",
  "user_country_code": ["DE", "NL"],
  "service_type": ["amnezia-premium"]
}
```

| Field | Type | Presence | Notes |
|---|---|---|---|
| `locale` | **string** | always | App language, ISO 639-1: `en`, `ru`, `uk`. Fall back to `en` for unknown values. |
| `user_country_code` | **array of strings** | optional | ISO 3166-1 alpha-2 codes. Absent when empty — never `null`, never a plain string. |
| `service_type` | **array of strings** | optional | Values: `amnezia-free`, `amnezia-premium`. Absent when empty — never `null`, never a plain string. |

**Important:** `user_country_code` and `service_type` are **arrays**, not scalars.
The client aggregates them as a *set union across all gateway (subscription)
servers* installed in the app (`ServersModel::GatewayStacks::toJson()`,
`servers_model.cpp:912`), so a user with servers in two countries sends
`["DE", "NL"]`. With a single server it is still an array of one element.
Both keys are omitted entirely when the set is empty — in that case return
generic (untargeted) news.

## Response payload (decrypted) — 200 OK

The client accepts **either** a bare array **or** an object with a `news` array.
Pick one and keep it stable; the examples below use the object form.

```json
{
  "news": [
    {
      "id": "2026-07-24-premium-sale",
      "title": "Summer sale: -30% on Premium",
      "content": "Until <b>August 1</b> Premium is 30% off. <a href=\"https://frkn.org\">Details</a>",
      "timestamp": "2026-07-24T12:00:00Z"
    },
    {
      "id": "2026-07-10-new-locations",
      "title": "New locations",
      "content": "Servers in Finland and Japan are now available.",
      "timestamp": "2026-07-10T09:30:00+03:00"
    }
  ]
}
```

Item fields:

| Field | Type | Required | Notes |
|---|---|---|---|
| `id` | string | yes | Stable unique id. The client stores read/unread state per id locally — **never reuse an id** for a different news item. |
| `title` | string | yes | Plain text, one line recommended. |
| `content` | string | yes | Rendered as **RichText** — a subset of HTML is allowed (`<b>`, `<a href>`, `<br>`, lists). |
| `timestamp` | string | yes | ISO 8601 (`Qt::ISODate`), with timezone, e.g. `2026-07-24T12:00:00Z`. |

Semantics:

- Empty news → return an empty array: `{"news": []}` and HTTP 200. This is the
  normal "nothing to show" case, not an error.
- Sorting is done on the client (newest first), but send newest first anyway.
- Read/unread state lives entirely on the client — the backend does not need
  per-user state.
- The response can be cached aggressively: the client fetches news on every app
  start (when a subscription server exists) and every time the user opens the
  News section. Short `Cache-Control` (e.g. 5 min) is fine; there is no
  conditional-request support on the client.

## Errors

| Situation | Recommended response | Client behavior |
|---|---|---|
| No news available | `200` + `{"news": []}` | Empty list shown. |
| Unknown/missing locale | `200`, fall back to `en` | — |
| Backend failure | `500` (+ encrypted error body) | Silent on background fetch; generic error popup when the user opened News explicitly. |

Avoid `404` / `409` / `501` for this endpoint — those codes have special
semantics in other client flows (config missing / version update required) and
would produce misleading error messages.

## Client flow (for context)

1. App start with a subscription server → silent `fetchNews(false)` — failures
   are ignored, list just stays empty.
2. Settings → News & Notifications → busy indicator + `fetchNews(true)` —
   failures show an error popup, then the (possibly cached/empty) list opens.
3. Items render in a list (`PageSettingsNewsNotifications`) and a detail page
   (`PageSettingsNewsDetail`); tapping an item marks it as read locally.
   Unread items show a dot on the settings entry.

## Testing without the app

The outer envelope makes plain `curl` impossible — an unencrypted
`POST /v1/news` returns `405` from the edge, same as other envelope-only
endpoints. Either test through the app (Dev console shows `[AGW REQUEST]` /
`[AGW RESPONSE]` logs with the decrypted bodies), or replicate the envelope
with your existing AGW test tooling used for `/v1/account_info`.

Reference decrypted request:

```json
{ "locale": "uk", "user_country_code": ["UA"], "service_type": ["amnezia-premium"] }
```

Minimal variant (fields omitted when empty):

```json
{ "locale": "en" }
```

Reference decrypted response:

```json
{ "news": [ { "id": "test-1", "title": "Test", "content": "Hello <b>world</b>", "timestamp": "2026-07-24T12:00:00Z" } ] }
```
