# IPv6 support — research & deferred plan (client + backend)

Status: **researched, deferred** (2026-08-28). Not scheduled.

Scope: Dopamine client (Qt/C++, AmneziaVPN fork) — iOS/macOS NE, Android, desktop (daemon);
protocols: WireGuard, AmneziaWG, VLESS (xray), Hysteria2 (inside xray-core).

## Goal (when picked up)

Allow a node to expose IPv6 entry addresses **in addition to** IPv4 ones, so the client
tries AWG/WG over IPv6 first and falls back to IPv4. Motivation: ISP/DPI environments
where IPv4 endpoints are poisoned but IPv6 is cleaner; also v6-only mobile networks.

## Current state (research findings)

### Tunnel IPv6 (user traffic inside the tunnel) — partially works

| Platform / protocol | Status | Details |
|---|---|---|
| WG/AWG, iOS/macOS NE | works | `PacketTunnelSettingsGenerator.swift:194-206` splits v4/v6 addresses+routes; `::/0` lands in `ipv6IncludedRoutes` (:245-254); endpoint excluded per family (:257-283) |
| WG/AWG, Android | works | `client/android/wireguard/.../Wireguard.kt:91-101` default routes `0.0.0.0/0` + `::/0`; `InetNetwork.parse` is v6-aware |
| WG, desktop (daemon) | limited | `client/mozilla/localsocketcontroller.cpp:147` assigns ULA `fd58:baa6:dead::1`; `::/0` routed only when allowed_ips is exactly `["0.0.0.0/0","::/0"]` (:170,:200-204); custom v6 subnets broken by hardcoded `isIpv6: false` (:180,:186,:214,:220) |
| xray (VLESS/H2), iOS | **off** | `client/platforms/ios/PacketTunnelProvider+Xray.swift:132` `ipv6Enabled = false` (hardcoded) |
| xray, Android | works | `client/android/xray/.../Xray.kt:90-91` routes `0.0.0.0/0` + `2000::/3` |
| xray, desktop | **off** | `client/protocols/xrayprotocol.cpp:219-223` calls `StopRoutingIpv6()` on purpose |

### IPv6 as a server endpoint (dial address) — NOT supported anywhere

- `connectionController.cpp:36` INI regex `^(\s*Endpoint\s*=\s*)[^:\s]+(:\d+\s*)$` — substituting a
  v6 literal produces `Endpoint = 2001:db8::1:51820` (no brackets, unparseable); an existing
  bracketed `[v6]:port` doesn't match the regex at all (patch silently skipped).
- `connectionController.cpp:52-57` — `"server"` split via `lastIndexOf(':')` mangles bare v6.
- iOS `WGConfig.swift:176,193` generates `Endpoint = host:port` without brackets; the Endpoint
  parser (`Endpoint.swift:50-58`) requires `[v6]:port`.
- Desktop: `localsocketcontroller.cpp:151` always fills `serverIpv4AddrIn`; `serverIpv6AddrIn`
  is commented out (:152). The daemon itself understands v6 endpoints
  (`wireguardutilsmacos.cpp:237-238` writes `endpoint=[...]:`) — it just never receives one.
- `installController.cpp:837-838` — `hostName.split(":")` breaks any v6 literal.
- Android: bare v6 in hostName actually survives (`InetEndpoint.parse`, hostName goes straight
  to `parseInetAddress`), but bracketed `[v6]` is not understood by `InetAddress.getByName`.
- node_ips pool (`connectionController.cpp:286-314`): values patched verbatim, no v6
  validation/bracketing — pool is IPv4-only by assumption.

### DNS64/NAT64 (v6-only client networks) — works on iOS

- `client/3rd/amneziawg-apple/Sources/WireGuardKit/DNSResolver.swift:94-137`
  (`withReresolvedIP`, iOS only): re-resolves with default flags so iOS NAT64/DNS64 synthesis
  kicks in; primary resolve uses `AI_ALL`.
- "DNS64: mapped X to itself" log = endpoint was already a numeric IPv4 literal; NAT64 applies
  to the UDP flow at OS level. No Android/desktop equivalent.

### Explicit IPv4-only filters in critical paths

- `client/vpnconnection.cpp:175,:414` — split-tunnel domain resolve takes first A record only.
- `client/ui/controllers/sitesController.cpp:38` — same when adding a site.
- xray v6 off (see table above).
- Positive: `client/mozilla/shared/ipaddress.cpp` is dual-stack; Windows/Linux daemons are v6-capable.

## Plan (deferred)

### Backend

- New optional field next to `node_ips`, e.g. `"node_ips_v6": ["2a0b:...::66"]` —
  same rules as `node_ips` (optional, only when the node really has v6; old clients ignore it;
  migration `ALTER TABLE nodes ADD COLUMN node_ips_v6 TEXT[]`).
- The endpoint embedded inside the config itself stays the primary **IPv4** address
  (backwards compatibility, same contract as node_ips).
- Until the client ships v6 endpoints: **do not put IPv6 literals into `node_ips`** — the pool
  assumes IPv4 and will break silently.

### Client (try v6 first, fall back to v4)

1. Address transport fixes:
   - `connectionController.cpp` patcher: detect v6 literals, bracket for INI (`[v6]:port`) and
     `"server"` host:port; JSON `address` fields (xray vnext / hysteria servers) take bare v6.
   - iOS `WGConfig.swift`: emit `[v6]:port` (parser already accepts it).
   - Desktop daemon controller: enable the `serverIpv6AddrIn` path.
2. Pool ordering in `connectToServerIndex`: `[v6 addrs..., v4 addrs...]`, shuffle within each
   family only — retries walk v6 first, then fall through to v4 (reuses the existing
   retry/traffic-gate machinery unchanged).
3. Only add v6 addresses to the pool when the client device actually has global IPv6
   (otherwise the first retry is guaranteed dead — costs one timeout, harmless but slow).
4. xray/H2 endpoints over v6: separate question — tunnel v6 is hardcoded off on iOS/desktop
   for xray, but *endpoint* over v6 is independent and easier; evaluate when we get there.

## Effort estimate

- Backend: mirror of the node_ips migration — small.
- Client patcher + iOS + Android: ~1 day incl. testing on a real dual-stack node.
- Desktop daemon path: a bit more.
