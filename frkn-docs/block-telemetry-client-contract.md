# Телеметрия блокировок — что клиент отправляет на самом деле

Документ для бэкенда: сверка «ТЗ (`block-telemetry-api.md`) ↔ реальный код клиента».
Всё ниже снято с кода `client/core/controllers/connectionReportController.cpp`
(версия клиента 4.8.14.75) и `client/core/controllers/gatewayController.cpp`.

Читать так: §1–§3 — то, что клиент делает безусловно; §4–§5 — точные payload'ы;
§6 — **чек-лист: что бэк обязан принять, даже если этого нет в первом ТЗ**;
§7 — дельта к ТЗ, чтобы не искать по тексту.

---

## 1. Транспорт: тот же шифрованный конверт, что у `/v1/services`

Клиент не ходит в сеть «просто JSON». Любой запрос идёт через
`GatewayController::postAsync(endpoint, payload)`, а он:

1. генерирует AES-ключ/iv/salt на запрос;
2. кладёт их в `key_payload`, зашифрованный RSA публичным ключом gateway;
3. шифрует `api_payload` (наш JSON) AES-ом;
4. отправляет `{key_payload, api_payload}` на `endpoint`, где `%1` подставляется
   gateway-endpoint'ом (`https://api.frkn.org/` или fallback);
5. **ответ обязан быть зашифрован тем же ключом/iv/salt** — клиент расшифровывает
   тело и только потом разбирает JSON.

Эндпойнты, которые клиент вызывает (строки в коде):

- `%1v1/report/block` — одиночный отчёт по текущему серверу;
- `%1v1/report/scan` — срез по всем серверам подписки.

Заголовки: `Content-Type: application/json`, `X-Client-Request-ID: <uuid>` (клиентский
таймаут запроса — 45 с, `apiDefs::requestTimeoutMsecs`).

**Следствие для бэка:** обработчик должен быть обёрнут так же, как
`post_amnezia_services_route`: `crypto::with_agw_decryption::<T>(agw_key)` на входе и
`crypto::encrypt_gateway_reply(response, ctx)` на выходе. Если ответить plaintext-JSON,
клиент не сможет его прочитать и покажет ошибку.

## 2. Авторизация

`auth_data` — это **объект, скопированный из `api_config` текущего сервера подписки**
(`servers_model` → роль `apiConfig` → ключ `auth_data`), а не что-то, что клиент
собирает сам. Поэтому:

- для обычной подписки там лежит `{"id": "<subscription uuid>"}`;
- для шар-подписки — `{"share_token": "..."}`;
- если у сервера нет `api_config` (импортированный вручную конфиг) — `auth_data` пустой,
  и клиент **не отправляет** отчёт вообще (кнопка «Send to support» ответит
  `error: "no subscription auth data"`).

Существующий `extract_subscription_id(auth_data)` в `handlers/amnezia.rs` читает
`auth_data.id` — этого достаточно для обычной подписки; для share-ветки нужен разбор
`auth_data.share_token`.

## 3. Настройка «телеметрия» — по умолчанию выключена

`Conf/telemetryEnabled` (default **false**, `client/settings.h`). Пока она выключена:

- `collect()` / `collectAll()` не запускаются;
- `send()` отвечает ошибкой `telemetry is disabled`;
- автосбор сессии не стартует (таймер не создаётся);
- в UI пункты отчётов в «Поддержка» скрыты, тумблер живёт в Настройки → Диагностика →
  «Share network telemetry».

То есть **пока пользователь сам не включит тумблер, на бэк не прилетит ничего** —
это осознанное решение, а не забытая галочка.

## 4. `POST /v1/report/block` — одиночный отчёт (кнопка «Connection report»)

```json
{
  "auth_data": { "id": "8b1f0c1e-…" },
  "report_id": "1f2c9d7e-…",
  "report": {
    "app": { "version": "4.8.14.75", "platform": "android", "os": "14" },
    "support_tag": "3f0c8a1e-…",
    "device": {
      "manufacturer": "samsung", "model": "SM-A536B",
      "os_version": "14", "sdk": 34, "locale": "ru_RU"
    },
    "network": {
      "carrier": "MegaFon", "mcc": "250", "mnc": "02",
      "network_country": "ru", "mobile": true
    },
    "target": {
      "host": "est1.frkn.org",
      "address": "93.89.220.49",
      "protocol": "awg",
      "port": 443,
      "country": "Estonia",
      "country_code": "EE"
    },
    "probe": {
      "dns": { "ips": ["93.89.220.49"] },
      "icmp": { "ok": true },
      "tcp": [ { "port": 443, "state": "timeout" } ],
      "udp": [ { "port": 443, "state": "no-answer" } ]
    },
    "session": {
      "connected": true,
      "connected_at": "2026-10-28T00:10:03Z",
      "uptime_s": 1188,
      "rtt_ms_avg": 63,
      "rtt_ms_p95": 84,
      "loss_pct": 1.6,
      "samples": [61, 63, 60, -1, 62],
      "disconnect": { "at": "2026-10-28T00:29:51Z" }
    },
    "at": "2026-10-28T00:30:00Z"
  }
}
```

Что здесь важно понимать:

| Поле | Как формируется | Что бывает |
|---|---|---|
| `report_id` | uuid4 на каждый сбор | всегда есть |
| `support_tag` | installation uuid (`SettingsController.getInstallationUuid`) | всегда есть |
| `target.address` | первый адрес из DNS-резолва хоста | **может быть пустой строкой**, если DNS не ответил |
| `target.port` | первый порт пробы (порт сервиса из конфига, иначе 443) | всегда есть |
| `target.protocol` | `serviceProtocol` из модели | `awg`, `awg-mobile`, `vless`, `hysteria2`, `wireguard` (как пришло из подписки) |
| `probe.icmp` | системный `ping`, только `ok` | `rtt_ms` **не отправляется** (клиент его не кладёт) |
| `probe.tcp[]` | TCP connect, 3 с | `open\|refused\|timeout\|no-host\|err`, `ms` только при `open`/`refused` |
| `probe.udp[]` | датаграмма + 1.5 с ожидания | `answered\|closed\|no-answer\|err` |
| `session` | автосбор при подключении | **может отсутствовать** (если сессии не было); `samples` — до 60 значений, `-1` = потеря |
| `session.disconnect` | при разрыве | пока только `at` |
| `device` | Android: `Build.MANUFACTURER/MODEL/VERSION.RELEASE/SDK_INT` + локаль | на iOS/macOS/Windows объект **отсутствует** |
| `network` | Android: `TelephonyManager` (carrier, MCC/MNC, network country) + `ConnectivityManager` (mobile) | на iOS/десктопе **отсутствует**; на части прошивок `carrier`/`network_country` приходят пустыми — это нормально |
| `error` | **не отправляется** | появится позже (код/стадия ошибки коннекта) |
| `note` | не отправляется | в UI нет поля ввода |

## 5. `POST /v1/report/scan` — срез по всем серверам (кнопка «Check all servers»)

```json
{
  "auth_data": { "id": "8b1f0c1e-…" },
  "scan_id": "5c1e0f7a-…",
  "scan": {
    "app": { "version": "4.8.14.75", "platform": "android", "os": "14" },
    "support_tag": "3f0c8a1e-…",
    "mode": "full",
    "started_at": "2026-10-28T00:31:00Z",
    "duration_ms": 41200,
    "network": { "mobile": true },
    "results": [
      { "host": "est1.frkn.org", "address": "93.89.220.49", "port": 443,
        "protocol": "awg", "transport": "udp", "udp": "no-answer" },
      { "host": "eu01.frkn.org", "address": "185.156.108.232", "port": 443,
        "protocol": "vless", "transport": "tcp", "tcp": "open", "rtt_ms": 38 },
      { "host": "nl3.frkn.org", "address": "78.17.117.249", "port": 443,
        "protocol": "awg", "transport": "udp", "udp": "skipped" }
    ]
  }
}
```

Особенности, которых нет в первом ТЗ:

1. **Запись содержит либо `tcp`, либо `udp`, но не оба.** Транспорт выбирается по
   протоколу: `awg`/`awg-mobile`/`wireguard`/`hysteria2` → UDP-проба, `vless` → TCP.
   Парсить надо терпимо к отсутствующему полю.
2. `tls` **не отправляется** (TLS-хендшейк не делаем).
3. `dns_ok` **не отправляется** — вместо него пустой/непустой `address`.
4. При отмене или общем таймауте (60 с) в поле состояния попадает `skipped` —
   это не ошибка сети, а «не успели проверить».
5. Список — **только серверы из подписки пользователя** (решение согласовано), максимум
   512 записей: все A/AAAA-адреса каждого хоста + `node_ips` мульти-IP нод, × порты.
6. Скан блокируется, пока поднят VPN (иначе пробы идут через туннель и меряют туннель).

## 6. Чек-лист бэкенду

Обязательно:

- [ ] обе ручки принимают **шифрованный** конверт gateway и отвечают **шифрованно**
      (`with_agw_decryption` + `encrypt_gateway_reply`);
- [ ] `auth_data.id` → subscription; `auth_data.share_token` → share-ветка;
      пустой/неизвестный `auth_data` → 401;
- [ ] идемпотентность по `report_id` / `scan_id` (повтор → `202 dedup:true`, без дубля);
- [ ] **не требовать** полей `error`, `note`, `probe.icmp.rtt_ms`,
      `scan.results[].tls`, `scan.results[].dns_ok` — клиент их сейчас не отправляет;
- [ ] `device` и `network` — **опциональные объекты**: на iOS/десктопе их нет вовсе,
      на Android отдельные поля могут быть пустыми строками (`carrier`, `network_country`);
      `mcc`/`mnc` приходят только если оператор их отдал;
- [ ] `target.address` может быть пустой строкой (DNS не ответил) — это валидный отчёт,
      а не 400; такие отчёты важны: они и означают «у человека не резолвится наш хост»;
- [ ] в `scan.results[]` запись содержит только одно из `tcp`/`udp`; `skipped`
      валиден и означает «не успели/отменили»;
- [ ] `protocol` сравнивать как строку из подписки: `awg` и `awg-mobile` — **разные**
      значения (в `proto_matches` это уже учтено, повторить в отчётах);
- [ ] ответ `202 {"id": "…", "dedup": false, "results_count": N}` — клиент смотрит
      только на факт отсутствия ошибки, но поле `status >= 400` в теле считает ошибкой;
- [ ] лимиты: тело ≤ 16 КБ (report) / ≤ 128 КБ (scan) → 413; частота → 429
      (клиент показывает «Failed: http 429»);
- [ ] не писать тело отчёта в общий лог сервиса: там адреса и порты серверов.

Желательно (клиент добавит позже, бэк должен быть готов):

- [ ] `report.error {code, stage}` — сейчас не шлётся, появится после того, как
      прокинут код ошибки из `connectionController`;
- [ ] `report.session.disconnect {code, stage}` — сейчас только `at`;
- [ ] `scan.results[].tls` — если решим делать TLS-хендшейк для reality-портов;
- [ ] `probe.icmp.rtt_ms` — если понадобится для сравнения качества каналов.

## 6.1 Сторы: что можно и чего нельзя

Отправляемое сознательно ограничено — это и требование Apple/Google, и наша же
минимизация данных:

- **никаких аппаратных идентификаторов**: ни IMEI, ни IMSI, ни серийника, ни MAC,
  ни рекламного id (GAID/IDFA) — мы их не читаем вообще;
- **никаких опасных разрешений**: `READ_PHONE_STATE`, `ACCESS_FINE_LOCATION`,
  `READ_SMS` не запрашиваются. Из нового добавлено одно **normal**-разрешение
  `ACCESS_NETWORK_STATE` (определить «мобильная сеть или Wi-Fi»), оно не показывает
  пользователю диалог и не относится к sensitive;
- **сбор выключен по умолчанию** (`Conf/telemetryEnabled = false`) и включается только
  вручную в Настройки → Диагностика; до этого ни один байт не уходит;
- отправка — **только по нажатию** «Send to support», фоновой отправки нет;
- передача — по HTTPS в AES/RSA-конверте gateway.

Что это значит для деклараций (сделать при релизе, не в коде):

- **Google Play → Data safety**: отметить сбор «App info and performance → Diagnostics»
  и «Device or other IDs» (наш installation uuid — app-scoped), шифрование в пути,
  «собирается по желанию пользователя» (optional), удаление по запросу;
- **Apple → App Privacy**: «Diagnostics» — не для трекинга, без связи с личностью;
  ATT не нужен, так как трекинга между приложениями нет;
- в Privacy Policy добавить абзац «диагностические отчёты»: что уходит (адрес сервера,
  пробы, оператор/регион, версия системы) и что не уходит никогда.

## 7. Дельта к первому ТЗ (`block-telemetry-api.md`)

| Что | В ТЗ было | В коде сейчас |
|---|---|---|
| Конверт запроса | plaintext JSON | **AES/RSA-конверт gateway** (как `/v1/services`) |
| `report.error` | обязательное поле | **не отправляется** |
| `report.device` | объект `{splittunnel, killswitch, mtu}` | **реализовано иначе**: `{manufacturer, model, os_version, sdk, locale}`, только Android |
| `report.probe.icmp.rtt_ms` | опционально | не отправляется |
| `report.session` | «появится в фазе 2» | **уже реализовано** (RTT раз в минуту, буфер 60) |
| `scan.results[].tls`, `dns_ok` | были в схеме | **не отправляются** |
| `scan.results[]` | tcp и udp у каждой записи | **одно из двух**, по протоколу |
| Список для скана | «подписка или весь инвентарь» | только серверы подписки (согласовано) |
| `network` (оператор/регион) | не было | `{carrier, mcc, mnc, network_country, mobile}`, только Android |
| Гейт на клиенте | не было | `Conf/telemetryEnabled`, default **false** |
