# Split-tunneling service presets — `POST /v1/split_presets`

Backend spec for the split-tunneling **service presets** feature in the Dopamine
client. Instead of typing domains by hand, the user ticks services
(«YouTube», «ChatGPT», «Gemini», «Instagram»…) and the client adds/removes the
whole domain bundle of that service to the split-tunneling site list.

Presets are delivered **dynamically from the backend** so the bundles can be
updated without an app release. The client caches the last successful response
for offline use.

## Endpoint

```
POST https://api.frkn.org/v1/split_presets
Content-Type: application/json
X-Client-Request-ID: <uuid4>
```

No auth token. Client timeout: **12 s**.

## Transport — AGW encrypted envelope

Identical to `/v1/news` (see `frkn-docs/api-news.md`): outer
`{keyPayload, apiPayload}`, AES-256-CBC payload, RSA-wrapped keys, response =
base64 AES ciphertext with the same key/IV/salt. An unencrypted body is a
decryption error on the client.

## Request payload (decrypted)

```json
{
  "locale": "ru",
  "presets_version": "2026-07-30.1"
}
```

| Field | Type | Presence | Notes |
|---|---|---|---|
| `locale` | string | always | App language, ISO 639-1: `en`, `ru`, `uk`. Fall back to `en`. Used only for preset display names. |
| `presets_version` | string | optional | The `version` of the client's cached preset list. Absent on first fetch. |

If `presets_version` matches the current version on the backend, the backend
MAY answer with `{ "version": "...", "presets": [] }` and the client keeps its
cache. Returning the full list every time is also fine — the payload is small.

## Response payload (decrypted) — 200 OK

```json
{
  "version": "2026-07-30.1",
  "presets": [
    {
      "id": "youtube",
      "name": "YouTube",
      "domains": [
        "youtube.com",
        "googlevideo.com",
        "ytimg.com",
        "youtube-nocookie.com",
        "youtubei.googleapis.com",
        "youtube.googleapis.com"
      ]
    },
    {
      "id": "chatgpt",
      "name": "ChatGPT",
      "domains": [
        "chatgpt.com",
        "openai.com",
        "oaistatic.com",
        "oaiusercontent.com",
        "chat.com"
      ]
    },
    {
      "id": "gemini",
      "name": "Gemini",
      "domains": [
        "gemini.google.com",
        "bard.google.com",
        "gstatic.com"
      ]
    },
    {
      "id": "instagram",
      "name": "Instagram",
      "domains": [
        "instagram.com",
        "cdninstagram.com",
        "ig.me"
      ]
    }
  ]
}
```

Fields:

| Field | Type | Required | Notes |
|---|---|---|---|
| `version` | string | yes | Any string that **changes whenever the list changes** (date-based is fine). The client refetches and reapplies on change. |
| `presets[].id` | string | yes | Stable unique id (`youtube`, `chatgpt`, …). **Never reuse** for a different service — the client stores toggle state per id. |
| `presets[].name` | string | yes | Display name (brand names need no real localization; still returned per `locale`). |
| `presets[].domains` | array of strings | yes | Plain host suffixes, **no scheme, no `*.` prefix**. A domain matches itself and all subdomains. |

Domain semantics (must match the client's site list):

- `googlevideo.com` covers `r1---sn-abc.googlevideo.com` and any other subdomain.
- Domains only — no IPs, no CIDRs, no paths.
- Keep bundles conservative: a preset must not accidentally cover half the
  internet (avoid `google.com`, `cloudflare.com` etc.). Prefer specific
  service domains over shared corporate parents.

Semantics:

- Empty list → `{ "version": "...", "presets": [] }` with HTTP 200 is the
  normal «nothing configured» case, not an error.
- The client applies toggles into the existing split-tunneling site list:
  enabling a preset adds all its domains (tagged with the preset id),
  disabling removes exactly those domains. Manually added domains are untouched.
- Sorting as sent; the client renders presets in response order.
- Cache-friendly: the client fetches silently on app start (when a
  subscription server exists) and when the user opens split-tunneling settings.

## Errors

| Situation | Recommended response | Client behavior |
|---|---|---|
| No presets configured | `200` + `{"version": "...", "presets": []}` | Presets section hidden, manual list unchanged. |
| Backend failure | `500` (+ encrypted error body) | Silent on background fetch — cached/last list used. |

Avoid `404` / `409` / `501` (special semantics in other client flows).

## Initial preset set (suggested)

| id | name | Notes |
|---|---|---|
| `youtube` | YouTube | see example above |
| `chatgpt` | ChatGPT | see example above |
| `gemini` | Gemini | see example above |
| `instagram` | Instagram | see example above |
| `tiktok` | TikTok | tiktok.com, tiktokv.com, tiktokcdn.com, musical.ly |
| `x` | X (Twitter) | x.com, twitter.com, twimg.com, t.co |
| `facebook` | Facebook | facebook.com, fb.com, fbcdn.net, fbsbx.com |
| `whatsapp` | WhatsApp | whatsapp.com, whatsapp.net |
| `telegram` | Telegram | telegram.org, t.me, telegra.ph, cdn-telegram.org |
| `netflix` | Netflix | netflix.com, nflxvideo.net, nflximg.net, nflxext.com |
| `spotify` | Spotify | spotify.com, scdn.co, spotifycdn.net |
| `discord` | Discord | discord.com, discord.gg, discordapp.com, discordcdn.com |

Review each bundle against the «conservative» rule above before publishing.

## Client flow (for context)

1. App start with a subscription server → silent fetch; failure ignored,
   cached list used.
2. Settings → Split tunneling → presets section with checkboxes on top of the
   manual site list; toggling applies the bundle into the site list immediately.
3. Toggle state (`preset id → on/off`) is stored locally per device; the
   backend does not need per-user state.
