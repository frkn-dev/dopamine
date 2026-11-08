# Телеметрия v2 — сэмплы для бэкенда

Что изменилось относительно первой версии отчётов (всё без новых разрешений на
устройстве, кроме уже выданных):

| Блок | Новые поля |
|---|---|
| `app` | `build` — дата/время сборки клиента (`__DATE__ __TIME__`) |
| `device` | теперь заполняется **и на iOS/macOS**: `model` (`iPhone16,2`), `model_ident` (macOS `Mac14,5`), `total_ram_mb`, `os_version`, `locale` |
| `network` | `ipv6_available` (bool), `disk_free_mb` (int) |
| `settings` | `mtu_override`, `reconnect_until_cancelled`, `post_connect_check`, `experimental`, `telemetry`, `kill_switch`, `app_language` |
| `probe.handshake[]` | `attempts` — сколько раз стучались (1 = с первой попытки) |
| `tunnel` | `transport_bytes {rx_bytes, tx_bytes}` — реально прошедшие байты за сессию |
| `session` | `prev {connected_at, uptime_s, endpoint, reason}` — сводка предыдущей сессии |
| `scan` | `settings`, `app_build` |

Ничего из этого не требует новых разрешений: модель/память — `sysctl`,
IPv6/свободное место — локальные интерфейсы и ФС, байты — счётчики NE,
настройки — уже хранятся в приложении.

---

## 1. Скан всех серверов (`POST /v1/report/scan`)

```jsonc
{
  "auth_data": { "id": "<installation id>", "api_key": "<...>" },
  "report_id": "7d1c0e0e-6f5a-4b1e-9f0e-6b2f0f5c9a11",
  "app": { "version": "4.8.14.79", "platform": "ios", "os": "26.6.2" },
  "scan_id": "b2f0c1d4-3f0a-4f0e-8a11-7c9f2b3d4e55",
  "scan": {
    "app": { "version": "4.8.14.79", "platform": "ios", "os": "26.6.2" },
    "app_build": "4.8.14.79 Oct 31 2026 18:42:03",
    "support_tag": "023d804b-e677-446e-aba4-d02742fb0e3a",
    "mode": "full",
    "started_at": "2026-10-31T15:02:10Z",
    "duration_ms": 18430,
    "device": {
      "model": "iPhone16,2",
      "total_ram_mb": 8192,
      "os_version": "26.6.2",
      "locale": "ru_RU"
    },
    "network": {
      "carrier": "MTS", "mcc": "250", "mnc": "01", "network_country": "ru",
      "mobile": true,
      "local_ip": "10.66.13.138", "iface": "pdp_ip0", "iface_type": "other",
      "ipv6_available": false,
      "disk_free_mb": 4180
    },
    "settings": {
      "mtu_override": 1280, "reconnect_until_cancelled": true,
      "post_connect_check": true, "experimental": false,
      "telemetry": true, "kill_switch": false, "app_language": "ru_RU"
    },
    "results": [
      { "host": "at.frkn.org", "address": "93.89.220.49", "port": 443, "protocol": "awg",
        "transport": "udp", "udp": "ok", "rtt_ms": 41, "attempts": 1,
        "disabled_by_user": false, "in_use": true, "config_hash": "19ab2b855b876974" },

      { "host": "at.frkn.org", "address": "77.221.152.88", "port": 8443, "protocol": "awg",
        "transport": "udp", "udp": "unreachable", "attempts": 3,
        "disabled_by_user": false, "in_use": false, "config_hash": "19ab2b855b876974" },

      { "host": "pl.frkn.org", "address": "195.133.80.155", "port": 443, "protocol": "awg-mobile",
        "transport": "udp", "udp": "ok", "rtt_ms": 78, "attempts": 1,
        "disabled_by_user": true, "in_use": false, "config_hash": "6c1d0a2f4b8e9a01" },

      { "host": "vless-eu.frkn.org", "address": "104.171.136.243", "port": 443, "protocol": "vless",
        "transport": "tcp", "tcp": "ok", "rtt_ms": 96,
        "disabled_by_user": false, "in_use": false, "config_hash": "8f3b1c7d2e4a6905" },

      { "host": "ru-msk.frkn.org", "address": "217.149.29.118", "port": 51820, "protocol": "wireguard",
        "transport": "udp", "udp": "unreachable", "attempts": 2, "error": "timeout",
        "disabled_by_user": false, "in_use": false, "config_hash": "aa71e2c9b34d5f60" }
    ]
  }
}
```

---

## 2. Карточка сервера, VPN выключен (`POST /v1/report/block`)

```jsonc
{
  "auth_data": { "id": "<installation id>", "api_key": "<...>" },
  "report_id": "3a9f5c22-1b7e-4b6c-8d33-5e0f1a2b3c44",
  "app": { "version": "4.8.14.79", "platform": "ios", "os": "26.6.2" },
  "report": {
    "app": { "version": "4.8.14.79", "platform": "iOS", "os": "26.6.2",
             "build": "4.8.14.79 Oct 31 2026 18:42:03" },
    "at": "2026-10-31T15:07:55Z",
    "support_tag": "023d804b-e677-446e-aba4-d02742fb0e3a",
    "target": {
      "host": "at.frkn.org", "address": "93.89.220.49", "port": 443,
      "protocol": "awg-mobile", "country": "Poland", "country_code": "PL"
    },
    "mtu": { "applied": 1280, "override": 1280, "server": 1420 },
    "config": {
      "api_protocol": "awg-mobile", "hash": "19ab2b855b876974",
      "rev": "82a5ac090387ca4fa1db2733478f512b660be58b37e0fe5f0a162144d0ce54e8",
      "size": 2009
    },
    "attempts": [
      { "at": "2026-10-31T14:58:02Z", "address": "77.221.152.88", "port": 8443,
        "protocol": "awg", "outcome": "timeout", "waited_ms": 20000 },
      { "at": "2026-10-31T14:58:25Z", "address": "93.89.220.49", "port": 443,
        "protocol": "awg", "outcome": "ok", "waited_ms": 1100 }
    ],
    "device": { "model": "iPhone16,2", "total_ram_mb": 8192,
                "os_version": "26.6.2", "locale": "ru_RU" },
    "network": {
      "carrier": "MTS", "mcc": "250", "mnc": "01", "network_country": "ru",
      "mobile": true, "local_ip": "10.66.13.138",
      "iface": "pdp_ip0", "iface_type": "other",
      "ipv6_available": false, "disk_free_mb": 4180
    },
    "settings": {
      "mtu_override": 1280, "reconnect_until_cancelled": true,
      "post_connect_check": true, "experimental": false,
      "telemetry": true, "kill_switch": false, "app_language": "ru_RU"
    },
    "probe": {
      "dns": { "ips": ["93.89.220.49"] },
      "handshake": [
        { "address": "93.89.220.49", "port": 443, "transport": "udp", "state": "ok",
          "ms": 41, "attempts": 1, "in_use": true, "disabled_by_user": false },
        { "address": "77.221.152.88", "port": 8443, "transport": "udp", "state": "unreachable",
          "attempts": 3, "in_use": false, "disabled_by_user": false },
        { "address": "195.133.80.155", "port": 443, "transport": "udp", "state": "ok",
          "ms": 78, "attempts": 1, "in_use": false, "disabled_by_user": true }
      ],
      "tcp": [], "udp": []
    },
    "session": { "connected": false }
  }
}
```

---

## 3. VPN включён, данные туннеля (`POST /v1/report/block`)

```jsonc
{
  "auth_data": { "id": "<installation id>", "api_key": "<...>" },
  "report_id": "c41d7a90-55b2-4d0e-9f77-1a2b3c4d5e66",
  "app": { "version": "4.8.14.79", "platform": "ios", "os": "26.6.2" },
  "report": {
    "app": { "version": "4.8.14.79", "platform": "iOS", "os": "26.6.2",
             "build": "4.8.14.79 Oct 31 2026 18:42:03" },
    "at": "2026-10-31T15:12:31Z",
    "mode": "tunnel",
    "support_tag": "023d804b-e677-446e-aba4-d02742fb0e3a",
    "session": {
      "connected": true,
      "connected_at": "2026-10-31T15:08:02Z",
      "uptime_s": 269,
      "rtt_ms_avg": 44, "rtt_ms_p95": 61, "loss_pct": 0,
      "samples": [44, 41, 46, 61, 43],
      "dns_check": "ok", "dns_ms": 28,
      "prev": {
        "connected_at": "2026-10-31T14:41:10Z",
        "uptime_s": 1310,
        "endpoint": "195.133.80.155",
        "reason": "disconnected"
      }
    },
    "tunnel": {
      "endpoint": "93.89.220.49",
      "port": 443,
      "mtu_applied": 1280,
      "client_ip": "10.100.16.143",
      "transport_bytes": { "rx_bytes": 1843210, "tx_bytes": 402113 }
    },
    "mtu": { "applied": 1280, "override": 1280, "server": 1420 },
    "config": {
      "api_protocol": "awg-mobile", "hash": "19ab2b855b876974",
      "rev": "82a5ac090387ca4fa1db2733478f512b660be58b37e0fe5f0a162144d0ce54e8",
      "size": 2009
    },
    "target": { "host": "at.frkn.org", "port": 443, "protocol": "awg-mobile" },
    "device": { "model": "iPhone16,2", "total_ram_mb": 8192,
                "os_version": "26.6.2", "locale": "ru_RU" },
    "network": {
      "carrier": "MTS", "mcc": "250", "mnc": "01", "network_country": "ru",
      "mobile": true, "local_ip": "10.66.13.138",
      "iface": "pdp_ip0", "iface_type": "other",
      "ipv6_available": false, "disk_free_mb": 4180,
      "tunnel_ip": "10.100.16.143"
    },
    "settings": {
      "mtu_override": 1280, "reconnect_until_cancelled": true,
      "post_connect_check": true, "experimental": false,
      "telemetry": true, "kill_switch": false, "app_language": "ru_RU"
    }
  }
}
```

---

## Типы для приёма (набросок)

```
transport_bytes: { rx_bytes: i64, tx_bytes: i64 }
settings: { mtu_override: u16, reconnect_until_cancelled: bool,
            post_connect_check: bool, experimental: bool,
            telemetry: bool, kill_switch: bool, app_language: string }
device: { model?: string, model_ident?: string, manufacturer?: string,
          total_ram_mb?: u32, os_version?: string, sdk?: u16, locale?: string }
network: { carrier?: string, mcc?: string, mnc?: string, network_country?: string,
           mobile?: bool, local_ip?: string, iface?: string, iface_type?: string,
           ipv6_available?: bool, disk_free_mb?: u32, tunnel_ip?: string }
probe.handshake[]: { address, port, transport, state, ms?, attempts?, in_use, disabled_by_user }
session.prev: { connected_at: string, uptime_s: i64, endpoint?: string, reason?: string }
```

Все новые поля приходят **только если есть данные**: старые клиенты их не шлют,
приёмник должен терпеть отсутствие любого из них (`Option<...>`).
