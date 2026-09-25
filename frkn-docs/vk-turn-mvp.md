# VK TURN MVP (Android)

Fallback transport: WireGuard UDP through VK Calls TURN (DTLS), user pastes their own call link.

## Stack

- Client: Dopamine Android (GPL-3) + `free-turn-proxy` client binary (Happy Bunny / permissive)
- Server (test VPS): `free-turn-proxy` server + plain WireGuard (not AWG for MVP)
- Flow: WG → `127.0.0.1:9000` → sidecar → VK TURN → VPS `:56000` → WG `:51820`

## App settings (Android)

- Enable VK TURN (Android 13+; older OS rejects connect with a clear error)
- Call link `vk.com/call/join/...`
- Peer host (optional; default = WG `hostName`)
- Peer port (default `56000`)
- Streams (default `2`)

## Binary

```bash
./deploy/fetch_vkturn_client.sh
```

Places `client-android-arm64` into `client/android/wireguard/src/main/assets/vkturn/` (gitignored).

## Test server (one VPS)

```bash
# WireGuard listens on 127.0.0.1:51820
./server -listen 0.0.0.0:56000 -connect 127.0.0.1:51820 -mode udp
```

Import a plain WG client config into Dopamine (Endpoint will be rewritten to localhost when VK TURN is on). Open UDP/TCP 56000 on the firewall.

## Manual test

1. Create VK group call, copy join link (do not "end for everyone").
2. Android: Connection → VK TURN → enable, paste link, set peer host/port if needed.
3. Connect to the WG server entry.
4. Works without whitelist/BS — use BS only to validate the fallback story.

## Not in MVP

iOS/desktop, auto rooms, AWG-on-server, catalog flag, captcha automation beyond what the binary does.
