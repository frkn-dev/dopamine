# Телеметрия: три варианта отчёта (примеры)

Все три уходят на `POST /v1/report/block` (скан — на `/v1/report/scan`), в теле
`auth_data` + `report_id` + `app` + (`report` | `scan`).

Режимы:

| # | Экран / ситуация | Что внутри | Эндпойнт |
|---|---|---|---|
| 1 | «Диагностика» → **Check all servers** | все адреса всех серверов подписки, реальный handshake | `v1/report/scan` |
| 2 | Карточка сервера, **VPN выключен** | один сервер: его пул, попытки, MTU, конфиг | `v1/report/block` |
| 3 | Карточка сервера, **VPN включён** | туннель: DNS через туннель, применённый MTU, сессия | `v1/report/block` |

---

## 1. Все серверы (скан из «Диагностики»)

Собирается только при выключенном VPN (иначе меряется туннель). До 25 с на
handshake, статусы галочек — реальные.

```jsonc
{
  "auth_data": { "id": "<installation id>", "api_key": "<...>" },
  "report_id": "7d1c0e0e-6f5a-4b1e-9f0e-6b2f0f5c9a11",
  "app": { "version": "4.8.14.79", "platform": "ios", "os": "26.6.2" },
  "scan_id": "b2f0c1d4-3f0a-4f0e-8a11-7c9f2b3d4e55",
  "scan": {
    "app": { "version": "4.8.14.79", "platform": "ios", "os": "26.6.2" },
    "support_tag": "023d804b-e677-446e-aba4-d02742fb0e3a",
    "mode": "full",
    "started_at": "2026-10-31T14:02:10Z",
    "duration_ms": 18430,
    "device": { "manufacturer": "Apple", "model": "iPhone 15 Pro Max", "os_version": "26.6.2", "locale": "ru_RU" },
    "network": {
      "carrier": "MTS", "mcc": "250", "mnc": "01", "network_country": "ru",
      "mobile": true, "local_ip": "10.66.13.138", "iface": "pdp_ip0", "iface_type": "other"
    },
    "results": [
      { "host": "at.frkn.org", "address": "93.89.220.49", "port": 443, "protocol": "awg",
        "transport": "udp", "udp": "ok", "rtt_ms": 41,
        "disabled_by_user": false, "in_use": true, "config_hash": "19ab2b855b876974" },

      { "host": "at.frkn.org", "address": "77.221.152.88", "port": 8443, "protocol": "awg",
        "transport": "udp", "udp": "unreachable",
        "disabled_by_user": false, "in_use": false, "config_hash": "19ab2b855b876974" },

      { "host": "pl.frkn.org", "address": "195.133.80.155", "port": 443, "protocol": "awg-mobile",
        "transport": "udp", "udp": "ok", "rtt_ms": 78,
        "disabled_by_user": true, "in_use": false, "config_hash": "6c1d0a2f4b8e9a01" },

      { "host": "vless-eu.frkn.org", "address": "104.171.136.243", "port": 443, "protocol": "vless",
        "transport": "tcp", "tcp": "ok", "rtt_ms": 96,
        "disabled_by_user": false, "in_use": false, "config_hash": "8f3b1c7d2e4a6905" },

      { "host": "ru-msk.frkn.org", "address": "217.149.29.118", "port": 51820, "protocol": "wireguard",
        "transport": "udp", "udp": "unreachable", "error": "timeout",
        "disabled_by_user": false, "in_use": false, "config_hash": "aa71e2c9b34d5f60" }
    ]
  }
}
```

---

## 2. Карточка сервера, VPN выключен (все ендпойнты пула)

```jsonc
{
  "auth_data": { "id": "<installation id>", "api_key": "<...>" },
  "report_id": "3a9f5c22-1b7e-4b6c-8d33-5e0f1a2b3c44",
  "app": { "version": "4.8.14.79", "platform": "ios", "os": "26.6.2" },
  "report": {
    "app": { "version": "4.8.14.79", "platform": "iOS", "os": "26.6.2" },
    "at": "2026-10-31T14:07:55Z",
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
      { "at": "2026-10-31T13:58:02Z", "address": "77.221.152.88", "port": 8443,
        "protocol": "awg", "outcome": "timeout", "waited_ms": 20000 },
      { "at": "2026-10-31T13:58:25Z", "address": "93.89.220.49", "port": 443,
        "protocol": "awg", "outcome": "ok", "waited_ms": 1100 }
    ],
    "device": { "manufacturer": "Apple", "model": "iPhone 15 Pro Max",
                "os_version": "26.6.2", "locale": "ru_RU" },
    "network": { "carrier": "MTS", "mcc": "250", "mnc": "01", "network_country": "ru",
                 "mobile": true, "local_ip": "10.66.13.138",
                 "iface": "pdp_ip0", "iface_type": "other" },
    "probe": {
      "dns": { "ips": ["93.89.220.49"] },
      "handshake": [
        { "address": "93.89.220.49", "port": 443, "transport": "udp", "state": "ok",
          "ms": 41, "in_use": true, "disabled_by_user": false },
        { "address": "77.221.152.88", "port": 8443, "transport": "udp", "state": "unreachable",
          "in_use": false, "disabled_by_user": false },
        { "address": "195.133.80.155", "port": 443, "transport": "udp", "state": "ok",
          "ms": 78, "in_use": false, "disabled_by_user": true }
      ],
      "tcp": [], "udp": []
    },
    "session": { "connected": false }
  }
}
```

Замечания по полям:
- `probe.handshake` — только те адреса, что реально пробовали; `state`: `ok` (есть `ms`) / `unreachable`;
- `tcp`/`udp` — только для соответствующих транспортов (VLESS — `tcp`), пустые массивы не шлём;
- `disabled_by_user` — галочка выключена человеком; `in_use` — адрес, к которому подключён сейчас (или последний);
- `attempts` — последние 5 из `Conf.connectAttempts` (`ok` / `timeout` / `error`, `detail` = код).

---

## 3. VPN включён (данные по туннелю)

Проб по адресам тут нет — мерить чужие серверы через туннель бессмысленно.
Зато есть DNS-проверка через туннель, MTU и сессия.

```jsonc
{
  "auth_data": { "id": "<installation id>", "api_key": "<...>" },
  "report_id": "c41d7a90-55b2-4d0e-9f77-1a2b3c4d5e66",
  "app": { "version": "4.8.14.79", "platform": "ios", "os": "26.6.2" },
  "report": {
    "app": { "version": "4.8.14.79", "platform": "iOS", "os": "26.6.2" },
    "at": "2026-10-31T14:12:31Z",
    "mode": "tunnel",
    "support_tag": "023d804b-e677-446e-aba4-d02742fb0e3a",
    "session": {
      "connected": true,
      "connected_at": "2026-10-31T14:08:02Z",
      "uptime_s": 269,
      "rtt_ms_avg": 44, "rtt_ms_p95": 61, "loss_pct": 0,
      "samples": [44, 41, 46, 61, 43],
      "dns_check": "ok", "dns_ms": 28,
      "last_no_data": null
    },
    "tunnel": {
      "endpoint": "93.89.220.49",
      "mtu_applied": 1280
    },
    "mtu": { "applied": 1280, "override": 1280, "server": 1420 },
    "config": {
      "api_protocol": "awg-mobile", "hash": "19ab2b855b876974",
      "rev": "82a5ac090387ca4fa1db2733478f512b660be58b37e0fe5f0a162144d0ce54e8",
      "size": 2009
    },
    "target": { "host": "at.frkn.org", "port": 443, "protocol": "awg-mobile" },
    "device": { "manufacturer": "Apple", "model": "iPhone 15 Pro Max",
                "os_version": "26.6.2", "locale": "ru_RU" },
    "network": { "carrier": "MTS", "mcc": "250", "mnc": "01", "network_country": "ru",
                 "mobile": true, "local_ip": "10.66.13.138",
                 "iface": "pdp_ip0", "iface_type": "other",
                 "tunnel_ip": "10.100.16.143" }
  }
}
```

Особенности режима 3:
- `session.dns_check` — резолв имени через туннель (`ok` / `fail`), `dns_ms`;
- `session.last_no_data` — если сработал сторож одностороннего трафика:
  `{"address":"177.177.20.172","port":443,"at":"...","reason":"one-way"}`;
- `samples` — RTT раз в 60 с, до 10 последних;
- `loss_pct` — доля неудачных выборок, `-1` в `samples` = проба не ответила.

---

## Что ещё можем добавить (без обновления после релиза)

Уже есть: `app`, `device` (только Android), `network` (carrier/mcc/mnc/mobile/iface/local_ip/tunnel_ip),
`mtu`, `config`, `attempts`, `probe.*`, `session.*`, `support_tag`.

**Дешёво и полезно (рекомендую добавить сейчас):**

| Поле | Откуда | Зачем |
|---|---|---|
| `device.model` / `os_version` / `locale` на **iOS/macOS** | `utsname`/`sysctl` (`hw.machine`), `NSLocale` | сейчас `device` заполняется только на Android — на iOS в отчёте пусто |
| `device.total_ram_mb`, `disk_free_mb` | `NSProcessInfo` / `QStorageInfo` | жалобы «не подключается» часто из-за нуля места/OOM |
| `network.iface_type` детальнее | iOS: `NWPathMonitor` (wifi/cellular/wired/expensive/constrained) | «на Wi-Fi работает, на LTE нет» |
| `network.ipv6_available` | `QNetworkInterface` / `NWPath` | половина «не подключается» — сломанный IPv6 у оператора |
| `network.wifi_rssi` / `cellular_rsrp` | iOS: `NEHotspotNetwork`; Android: `TelephonyManager` | слабый сигнал объясняет потери без вины сервера |
| `tunnel.transport_bytes` (rx/tx за сессию) | счётчики NE/daemon | «подключено, но 0 байт» — самый частый кейс |
| `settings.*` (mtu_override, reconnect_until_cancelled, split_tunneling_mode, experimental, kill_switch) | настройки клиента | понять, что у человека не дефолт |
| `probe.handshake[*].attempts` | счётчик попыток пробы | отличать «не ответил 1 раз» от «не ответил 3 раза» |
| `app.build` (`__DATE__`) + `config_rev` | уже есть в `diagBuild` | проверять, что у человека именно та сборка |
| `prev_session` (краткая сводка прошлой сессии) | память/настройки | для автоотчёта при обрыве: сколько жило, последние RTT |

**Требует разрешений/осторожно (спросить, надо ли):**

- SSID/BSSID Wi-Fi — только с разрешения на геолокацию, считать персональными данными;
- IP-адрес шлюза/публичный IP — публичный не шлём сознательно (backend видит сам);
- список приложений / трафик по приложениям — не собирать;
- battery level/thermal state — можно, но объяснять в политике.

**Итого:** если хотим «один раз и надолго» — добавить сейчас: `device` на iOS,
`ipv6_available`, `tunnel.transport_bytes`, `settings.*`, `probe.handshake.attempts`,
`prev_session`. Это ~10 полей и не требует новых разрешений.
