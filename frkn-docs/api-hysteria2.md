# Hysteria2 — backend spec

How to add Hysteria2 (hy2) support so the Dopamine client picks it up the same way as
AWG and VLESS. **No client release is required for the config format below** — hy2 runs
as an outbound inside the bundled xray-core (v1.260724.0, supports `hysteria2`),
through the same Network Extension pipeline as VLESS.

## Where hy2 plugs into the existing flow

The client treats hy2 as **another xray outbound** — identical delivery to VLESS:

```
/v1/services  →  service card + connections (tag per connection)
/v1/config    →  per-connection container config
```

1. **`/v1/services`** — return hy2 connections inside the service (premium card or a
   separate one — client's choice). Each connection gets its own `connection_uuid`,
   `country_code`, and a label, e.g. `"BelySlon 🐘 · Hysteria2"`. The connection tag
   determines which inbound config is built (same rule as VlessTcpReality/VlessXhttpCdn).
2. **`/v1/config`** — for hy2 connections, return the **same container shape as xray**:

```json
{
  "containers": [
    {
      "container": "amnezia-xray",
      "xray": {
        "config": "<JSON string>",
        "isThirdPartyConfig": true,
        "last_config": "<same JSON string>"
      }
    }
  ]
}
```

3. **Delivery format** — identical to VLESS: the whole server config JSON in the
   `config` field, base64url (no padding), plain JSON (no gzip) — exactly like the
   current vless responses.

## The xray config string (hy2 outbound)

Contents of `config` / `last_config` (JSON string, outbounds-only; the client injects
the local SOCKS inbound itself):

```json
{
  "outbounds": [
    {
      "protocol": "hysteria2",
      "settings": {
        "servers": [
          {
            "address": "hy2.example.com",
            "port": 443,
            "password": "STRONG_SECRET",
            "serverName": "hy2.example.com"
          }
        ]
      },
      "streamSettings": {
        "network": "udp",
        "security": "tls",
        "tlsSettings": {
          "serverName": "hy2.example.com",
          "fingerprint": "chrome",
          "alpn": ["h3"],
          "allowInsecure": false
        }
      }
    }
  ]
}
```

Field mapping (client passes them through verbatim to xray-core):

| Field | Required | Notes |
|---|---|---|
| `address`, `port` | yes | hy2 node endpoint (UDP) |
| `password` | yes | hy2 auth password |
| `serverName` | yes | TLS SNI; also used for cert verification |
| `tlsSettings.alpn` | recommended | must be `["h3"]` for hy2 |
| `tlsSettings.fingerprint` | recommended | `chrome` |
| `tlsSettings.allowInsecure` | no | `true` only for self-signed/stealth setups |
| `obfs` (settings) | no | `{"type": "salamander", "password": "..."}` if the node uses obfs |
| `up_mbps` / `down_mbps` (settings) | no | bandwidth hints; omit = brutal CC auto |

## Subscription link format (for sharing / other clients)

hy2 link for external clients (Streisand, sing-box clients):

```
hy2://STRONG_SECRET@hy2.example.com:443?sni=hy2.example.com&insecure=0&obfs=salamander&obfs-password=OBFS_PW#FRKN-Hy2
```

## Checklist (backend)

- [ ] hy2 inbound on the node (hysteria2 server), UDP port open (e.g. 443/udp), cert for the SNI
- [ ] `/v1/services`: hy2 connections with own `connection_uuid` + label
- [ ] `/v1/config`: xray container (`amnezia-xray`) with the outbound JSON above,
      `users[].id` — N/A (hy2 uses `password`, not uuid)
- [ ] Same base64url-no-padding delivery as vless (not the awg gzip one)
- [ ] api_config.service_protocol: `"hysteria2"` (the client displays it as-is)

## What the client needs (already done)

- xray-core v1.260724.0 with `hysteria2` outbound — shipped in 4.8.14 (18)+
- hy2:// link parser — in progress on the client side; configs from the API work
  without it (they come as xray JSON)
