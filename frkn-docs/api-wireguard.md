# WireGuard (plain WG) — backend spec

How to add plain WireGuard support so the Dopamine client picks it up the same way as
AWG, VLESS and Hysteria2. **No client release is required** — shipped in 4.8.14 (23)+:
the client understands `service_protocol: "wireguard"`, generates WG keypairs and sends
`public_key` in `/v1/config` (previously only for `awg`).

## Where WG plugs into the existing flow

Identical to AmneziaWG — the only differences are the protocol tag and the container
name (`amnezia-wireguard`, **not** `amnezia-awg`):

```
/v1/services  →  service card + connections (service_protocol: "wireguard")
/v1/config    →  per-connection container config (WG INI + last_config JSON)
```

### 1. `/v1/services`

Return WG connections inside the service card (same card as VLESS/AWG/H2 — the client
shows one server list with a protocol filter). Per connection:

```json
{
  "connection_uuid": "9f8e7d6c-....",
  "country_code": "FI",
  "country_name": "FI",
  "service_protocol": "wireguard",
  "env": "dev",
  "connection_label": "Suomi 🏴‍☠️ · WireGuard"
}
```

- `service_protocol: "wireguard"` — required, drives the client's key exchange
  (see below) and the UI filter label.
- `connection_label` — format `"<name> · WireGuard"`; the client shows the name in the
  title and the protocol part in the small subtitle line.
- Also add `"wireguard"` to the card's `supported_protocols` array.

### 2. `/v1/config` request

The client sends the same envelope as for AWG, including a `public_key` field
(client-generated). **For the current release this field can be ignored** — see the
key-exchange section below: WG uses the AWG-style server-generated flow for now.

### Key exchange: one flow for WG and AWG (current release)

To keep WG and AWG identical, **both use the AWG-style server-side keypair**:

- the server generates the WG keypair, registers the derived pubkey as the peer;
- `client_priv_key` in the config (INI + `last_config`) contains the **real** private
  key (delivery is encrypted by the AGW envelope);
- the client takes the server-provided private key, persists it and derives the public
  key itself.

Post-release migration (do not forget — the two flows coexist in the codebase):
switch **both WG and AWG** to client-generated keys — the client sends only
`public_key`, the config carries the `$WIREGUARD_CLIENT_PRIVATE_KEY` placeholder, and
the private key never leaves the device. Compatibility rule for the migration:
`peer_pubkey = request public_key if present, else derived from the server privkey` —
old clients keep working with server-generated configs, new clients use their own keys.

Warning: do not mix the flows — if the node registers a derived/server pubkey but the
client signs with its own private key (or vice versa), the handshake never completes and
traffic is blackholed (same class of bug as the earlier VLESS uuid issue).

### 3. `/v1/config` response

Same delivery as AWG: whole server config JSON in `config`, **base64 standard of
gzip(JSON)**. Container shape:

```json
{
  "defaultContainer": "amnezia-wireguard",
  "containers": [
    {
      "container": "amnezia-wireguard",
      "amnezia-wireguard": {
        "config": "<INI string, see below>",
        "isThirdPartyConfig": true,
        "last_config": "<JSON string, see below>"
      }
    }
  ],
  "api_config": {
    "service_protocol": "wireguard"
  }
}
```

`config` — wg-quick INI with the server-generated client private key:

```ini
[Interface]
PrivateKey = <server-generated client WG private key>
Address = 10.8.1.2/32
DNS = 1.1.1.1, 1.0.0.1
MTU = 1420

[Peer]
PublicKey = <server WG public key>
PresharedKey = <psk>
AllowedIPs = 0.0.0.0/0, ::/0
Endpoint = wg.example.com:51820
PersistentKeepalive = 25
```

`last_config` — JSON string with the same parameters (the client syncs both):

```json
{
  "client_priv_key": "<server-generated client WG private key>",
  "client_pub_key": "<derived from client_priv_key>",
  "server_pub_key": "<server WG public key>",
  "psk_key": "<psk>",
  "client_ip": "10.8.1.2",
  "hostName": "wg.example.com",
  "port": "51820",
  "mtu": "1420",
  "persistent_keep_alive": "25"
}
```

**No AWG junk parameters** (`Jc`, `Jmin`, `Jmax`, `S1`–`S4`, `H1`–`H4`) — those are
AmneziaWG obfuscation and must be omitted for plain WireGuard.

## Rollout note

WG connections are wanted for **European users/nodes first** (EU countries in
`country_code`). The client does no region gating — whatever connections
`/v1/services` returns are shown; the protocol filter gets a «WireGuard» entry
automatically once such connections exist.

## Checklist (backend)

- [ ] WG server on the node (UDP port open, e.g. 51820/udp); server-generated client
      keypairs, peer = derived pubkey (same as AWG — migration to client keys is
      post-release, see the key-exchange section)
- [ ] `/v1/services`: WG connections with own `connection_uuid`,
      `service_protocol: "wireguard"`, label `"<name> · WireGuard"`;
      `"wireguard"` in card `supported_protocols`
- [ ] `/v1/config`: container **`amnezia-wireguard`** (not `amnezia-awg`),
      INI in `config`, JSON in `last_config`, `isThirdPartyConfig: true`
- [ ] base64(gzip(JSON)) delivery — same as AWG
- [ ] `api_config.service_protocol: "wireguard"`
- [ ] EU nodes first

## What the client already does (4.8.14 (23)+)

- `service_protocol: "wireguard"` → same import pipeline as AWG
- Parses the `amnezia-wireguard` container (INI + `last_config`), persists the
  server-provided private key, derives the public key from it
- (Post-release migration ready: also accepts the client-side flow — own keypair +
  `public_key` in `/v1/config` + placeholder in the config)
- Runs plain WG through the same Network Extension pipeline as AWG
- Shows «WireGuard» in the protocol filter; country flag from `country_code`
