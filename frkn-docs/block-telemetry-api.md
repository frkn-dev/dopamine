# Телеметрия блокировок — ТЗ на бэкенд (ffcore)

Основание: `frkn-docs/block-telemetry-research.md` (что собираем и зачем), клиентская
часть уже написана: `client/core/controllers/connectionReportController.{h,cpp}`
(собирает отчёт по текущему серверу и копирует его в буфер обмена).

Документ описывает **только серверную часть** и те минимальные доработки клиента,
без которых ручка бесполезна.

---

## 1. Цель

По одному отчёту из приложения научиться отвечать на вопрос: «у этого пользователя
заблокирован конкретный адрес:порт его оператором, или у нас умерла нода/сеть».
Ответ строится на сопоставлении трёх независимых источников:

| Источник | Что даёт | Где уже есть |
|---|---|---|
| Отчёт клиента | пробы DNS/ICMP/TCP/UDP по адресу и портам сервера + код ошибки коннекта | новая ручка (этот документ) |
| ASN оператора (по IP запроса) | «сколько разных операторов жалуются на один адрес» | считаем на сервере |
| Наш полигон `env/node-probe` | независимая проба того же адреса из РФ (раз в 20 мин, `latest.json`) | есть, крутится |
| Статус ноды | нода жива для других | `GET /status`, `NodeResponse` |

Один отчёт ничего не доказывает — доказательство появляется при агрегации.

## 2. Границы

**Делаем:** приём отчёта, валидацию, хранение, обогащение ASN/страной, часовой
роллап, агрегатную ручку для админки, детект «подозрение на блокировку» и алерт,
сверку с полигоном.

**Не делаем на этом этапе:** автоматическую отправку без действия пользователя,
хранение IP пользователя в открытом виде, содержимое трафика, привязку к биллингу,
машинное обучение, GeoIP по внешним HTTP-сервисам в горячем пути.

## 3. Зафиксированные решения

1. **Ручка живёт в том же шифрованном конверте, что и остальные gateway-ручки.**
   Первоначально предполагался plaintext, но в клиенте весь gateway-трафик уже идёт
   через `GatewayController` (`prepareRequest` → RSA+AES, `postAsync`), и он же
   расшифровывает ответы. Делать отдельный plaintext-путь ради одной ручки — лишний
   код в клиенте и лишний способ ошибиться. Поэтому:

   - сервер: `.and(crypto::with_agw_decryption::<ReportRequest>(agw_key))` и
     `crypto::encrypt_gateway_reply(response, ctx)`, ровно как у `/v1/services`
     (`routes.rs:863`);
   - клиент: `GatewayController::postAsync("%1v1/report/block", payload)` — без нового
     сетевого кода;
   - бонус: содержимое отчёта не видно в трафике, а там есть адреса и порты серверов,
     на которые смотрит подписка.
2. **Авторизация тем же `auth_data`, что у остальных ручек**: `{"id": "<subscription uuid>"}`
   (см. `extract_subscription_id` в `handlers/amnezia.rs`) либо `{"share_token": "..."}`.
   Нет валидного `auth_data` → `401`, отчёт не принимается: иначе это открытый
   приёмник мусора.
3. **Идемпотентность по `report_id`** (uuid4, генерирует клиент). Повторный POST с
   тем же `report_id` возвращает `202` и `"dedup": true`, второй записи не создаёт.
4. **IP пользователя не храним в открытом виде.** Храним: ASN (int), страну (2 буквы),
   префикс `/24` (для IPv4) / `/48` (IPv6) и `blake3(ip + salt)` для подсчёта
   уникальности. Соль в конфиге, ротация раз в 90 дней (старые хеши не пересчитываем —
   они нужны только для счётчика «сколько уникальных хостов за окно»).
5. **Ретенция:** сырые отчёты — 30 дней, часовой роллап — 12 месяцев.

## 4. API

### 4.1 `POST /v1/report/block` — приём отчёта

Роут: `src/bin/api/http/routes.rs` (по образцу `post_amnezia_services_route`, вместе с
`crypto::with_agw_decryption` и `encrypt_gateway_reply`), хендлер:
`src/bin/api/http/handlers/report.rs`, стор: `src/bin/api/postgres/report.rs`
(+ регистрация в `postgres/mod.rs` рядом с остальными сторами).

Тело запроса/ответа — в AES-конверте gateway, как у `/v1/services`; ниже поля
приведены в открытом виде (то, что видит хендлер после расшифровки).

Тело запроса:

```json
{
  "auth_data": { "id": "8b1f0c1e-…" },
  "report_id": "0f3f9a2e-…",
  "report": {
    "support_tag": "DOP-XXXX-XXXX",
    "app":    { "version": "4.8.14.72", "platform": "windows", "os": "10.0.19045" },
    "target": { "host": "est1.frkn.org", "address": "93.89.220.49",
                "port": 443, "protocol": "awg", "transport": "udp",
                "country": "Estonia", "country_code": "EE" },
    "error":  { "code": 1300, "stage": "handshake", "when": "2026-10-26T12:00:00Z" },
    "probe": {
      "dns":  { "host": "est1.frkn.org", "ips": ["93.89.220.49"] },
      "icmp": { "ok": true, "rtt_ms": 41 },
      "tcp":  [ { "port": 443, "state": "timeout" }, { "port": 8443, "state": "open", "ms": 38 } ],
      "udp":  [ { "port": 443, "state": "no-answer" } ]
    },
    "device": { "splittunnel": true, "killswitch": true, "mtu": 1280 },
    "session": {
      "connected": true,
      "connected_at": "2026-10-26T11:40:12Z",
      "uptime_s": 1188,
      "rtt_ms_avg": 63, "rtt_ms_p95": 84, "loss_pct": 1.6,
      "samples": [ 61, 63, 60, 118, 62 ],
      "disconnect": { "code": 1300, "stage": "handshake", "at": "2026-10-26T11:59:58Z" }
    },
    "note": "свободный текст пользователя, ≤ 500 символов",
    "at": "2026-10-26T12:00:03Z"
  }
}
```

Поля:

| Поле | Обяз. | Тип / значения | Примечание |
|---|---|---|---|
| `auth_data.id` \| `auth_data.share_token` | да (одно из) | uuid / строка | проверяется по БД |
| `report_id` | да | uuid4 | идемпотентность |
| `report.app.version` | да | `major.minor.patch.tweak` | ≤ 32 симв. |
| `report.app.platform` | да | `windows`\|`macos`\|`android`\|`ios`\|`linux` | приведение к lower-case |
| `report.app.os` | нет | строка ≤ 32 | |
| `report.target.address` | да | IPv4/IPv6 | нормализуется в `inet` |
| `report.target.port` | да | 1…65535 | основной порт коннекта |
| `report.target.host` | нет | строка ≤ 255 | домен |
| `report.target.protocol` | да | `awg`, `awg-mobile`, `wireguard`, `vless`, `hysteria2` | тот же набор, что `proto_matches` |
| `report.target.transport` | нет | `udp`\|`tcp` | |
| `report.error.code` | нет | int | `ServerConnectionTimeoutError = 1300` и прочие из `client/core/defs.h` |
| `report.error.stage` | нет | `connect`\|`handshake`\|`auth`\|`none` | |
| `report.probe.tcp` / `udp` | нет | массив ≤ 8 элементов | `state`: `open`,`refused`,`timeout`,`no-answer`,`closed`,`no-host`,`err`,`answered` |
| `report.probe.icmp.ok` / `rtt_ms` | нет | bool / int | |
| `report.probe.dns.ips` | нет | массив ≤ 8 | |
| `report.device` | нет | объект ≤ 8 ключей | только простые типы |
| `report.session` | нет | объект | собирается автоматически, пока коннект активен (см. §13); `samples` ≤ 60 целых |
| `report.session.disconnect` | нет | `{code, stage, at}` | заполняется при разрыве |
| `report.note` | нет | ≤ 500 симв. | вырезать управляющие символы |
| `report.at` | да | ISO-8601 UTC | допустимый перекос: −7 дней … +5 минут |

Ответы:

| Код | Тело | Когда |
|---|---|---|
| `202` | `{"id":"<uuid>","dedup":false}` | принят |
| `202` | `{"id":"<uuid>","dedup":true}` | такой `report_id` уже был |
| `400` | `{"status":400,"message":"invalid report: <поле>"}` | валидация |
| `401` | `{"status":401,"message":"auth failed"}` | нет/неизвестен `auth_data` |
| `413` | — | тело > 16 КБ |
| `429` | `{"status":429,"message":"too many reports"}` | лимиты |

### 4.2 `GET /v1/admin/report/block` — агрегат для админки

Авторизация как у остальных `/v1/admin/*`. Параметры: `from`, `to` (ISO, по умолчанию
последние 24 ч), `address` (фильтр), `group_by` = `address|port|asn|country|node`
(по умолчанию `address,port`), `min_reports` (по умолчанию 1).

Ответ — массив строк роллапа: `address`, `port`, `protocol`, `country`, `asn`,
`reports`, `distinct_asn`, `tcp_fail`, `udp_fail`, `icmp_fail`, `first_at`, `last_at`,
`verdict` (`blocked`|`node_offline`|`local_ok`|`unknown`).

### 4.3 `GET /v1/report/block/{id}` — статус (фаза 2, для поддержки)

Отдаёт вердикт по конкретному отчёту, чтобы поддержка могла сказать «мы видим, что
у вас блокирует оператор». Требует того же `auth_data`, что и приём.

### 4.4 `POST /v1/report/scan` — приём полного скана доступности

Тот же шифрованный конверт, что и §4.1. Отдельная ручка, потому что это другой объём и другая семантика: не «у меня не
подключилось», а «вот состояние всех наших адресов с этой точки». Нужна, чтобы
сравнивать точки между собой (у оператора X недоступны 3 адреса из 40, у Y — все 40).

```json
{
  "auth_data": { "id": "8b1f0c1e-…" },
  "scan_id": "5c1e0f7a-…",
  "scan": {
    "support_tag": "DOP-XXXX-XXXX",
    "app": { "version": "4.8.14.72", "platform": "android", "os": "14" },
    "mode": "full",
    "started_at": "2026-10-26T12:00:00Z",
    "duration_ms": 41200,
    "network": { "asn_known": false, "mobile": true },
    "results": [
      { "host": "est1.frkn.org", "address": "93.89.220.49", "port": 443,
        "protocol": "awg", "transport": "udp", "dns_ok": true,
        "tcp": "skipped", "udp": "no-answer", "rtt_ms": null },
      { "host": "est1.frkn.org", "address": "93.89.220.49", "port": 8443,
        "protocol": "vless", "transport": "tcp", "dns_ok": true,
        "tcp": "open", "tls": "ok", "rtt_ms": 38 },
      { "host": "nl3.frkn.org", "address": "78.17.117.249", "port": 443,
        "protocol": "awg", "transport": "udp", "dns_ok": false,
        "tcp": "timeout", "udp": "no-answer", "error": "dns" }
    ]
  }
}
```

| Поле | Обяз. | Тип / значения |
|---|---|---|
| `scan_id` | да | uuid4 (идемпотентность) |
| `scan.mode` | да | `full` (все адреса) \| `current` (только текущий сервер) |
| `scan.started_at`, `duration_ms` | да | ISO UTC, int |
| `scan.network.mobile` | нет | bool — мобильная сеть или Wi-Fi |
| `scan.results[]` | да | 1…512 записей; поля: `host`, `address` (IP), `port`, `protocol`, `transport`, `dns_ok`, `tcp` (`open\|refused\|timeout\|no-host\|err\|skipped`), `tls` (`ok\|fail\|skipped`), `udp` (`answered\|closed\|no-answer\|err\|skipped`), `rtt_ms`, `error` |

Ответы: `202 {"id":"…","results_count":N,"dedup":false}`, `400`, `401`,
`413` (тело > 128 КБ), `429`.

Лимиты: 2 скана в час на подписку, 1 скан в 10 минут на IP — это десятки-сотни
исходящих соединений, без лимита ручка станет инструментом сканирования чужих
сетей.

Важно про семантику: **UDP «no-answer» ничего не доказывает** (UDP не отвечает и
когда всё хорошо). Такой результат хранится и используется только в сравнении:
«раньше по этому адресу UDP был no-answer и коннект работал, теперь no-answer и
коннект не работает» → сигнал есть; сам по себе — нет.

## 5. Валидация и защита

- `warp::body::content_length_limit(16 * 1024)`.
- Строки обрезаются до лимитов выше; для строк снаружи — только `[A-Za-z0-9 ._:-]`.
- `InetAddr` парсится строго, мусор → `400`.
- Rate limit (по образцу `RateLimiter::new(...)` в `routes.rs`):
  - 10 отчётов / 60 с на IP;
  - 30 отчётов / час на `subscription_id`;
  - 3 отчёта / 10 мин на пару (`subscription_id`, `address`, `port`) — сверх лимита
    ответ `202 dedup=true` без записи (это тот же инцидент, а не новый).
- Отчёты с `at` в будущем > 5 минут или старше 7 дней отбрасываются (`400`).

## 6. Хранение (PostgreSQL, `tokio-postgres`)

Стор `postgres/report.rs` по образцу `postgres/install_token.rs`: `ensure_table()`
вызывается на старте, `PgClientManager` — через `Arc<Mutex<…>>`.

```sql
CREATE TABLE IF NOT EXISTS block_reports (
    id              uuid PRIMARY KEY,
    subscription_id uuid,
    share_token_hash bytea,
    support_tag     text,
    app_version     text NOT NULL,
    platform        text NOT NULL,
    os              text,
    node_address    inet NOT NULL,
    node_host       text,
    port            int  NOT NULL,
    protocol        text NOT NULL,
    transport       text,
    error_code      int,
    error_stage     text,
    icmp_ok         boolean,
    icmp_rtt_ms     int,
    dns_ips         jsonb,
    tcp_probe       jsonb,
    udp_probe       jsonb,
    device          jsonb,
    session         jsonb,
    user_note       text,
    asn             int,
    country         char(2),
    ip_prefix       cidr,
    ip_hash         bytea,
    report_at       timestamptz NOT NULL,
    created_at      timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS block_reports_addr_time ON block_reports (node_address, port, created_at DESC);
CREATE INDEX IF NOT EXISTS block_reports_asn_time  ON block_reports (asn, created_at DESC);
CREATE INDEX IF NOT EXISTS block_reports_sub_time  ON block_reports (subscription_id, created_at DESC);
```

```sql
CREATE TABLE IF NOT EXISTS block_report_rollup_hourly (
    bucket_hour   timestamptz NOT NULL,
    node_address  inet NOT NULL,
    port          int NOT NULL,
    protocol      text NOT NULL,
    country       char(2),
    asn           int,
    reports       int NOT NULL,
    distinct_asn  int NOT NULL,
    tcp_fail      int NOT NULL DEFAULT 0,
    udp_fail      int NOT NULL DEFAULT 0,
    icmp_fail     int NOT NULL DEFAULT 0,
    verdict       text NOT NULL DEFAULT 'unknown',
    PRIMARY KEY (bucket_hour, node_address, port, protocol, country, asn)
);
```

Партиционирование не нужно на старте (ожидаемый объём — единицы тысяч строк в месяц),
но `DELETE FROM block_reports WHERE created_at < now() - interval '30 days'` раз в час —
обязателен.

Сканы (§4.4) — отдельные таблицы, потому что это «срез по всем адресам», а не инцидент:

```sql
CREATE TABLE IF NOT EXISTS block_scan_runs (
    id              uuid PRIMARY KEY,
    subscription_id uuid,
    support_tag     text,
    app_version     text NOT NULL,
    platform        text NOT NULL,
    mode            text NOT NULL,          -- full | current
    is_mobile       boolean,
    started_at      timestamptz NOT NULL,
    duration_ms     int NOT NULL,
    results_count   int NOT NULL,
    asn             int,
    country         char(2),
    ip_prefix       cidr,
    ip_hash         bytea,
    received_at     timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS block_scan_results (
    scan_id     uuid NOT NULL REFERENCES block_scan_runs(id) ON DELETE CASCADE,
    host        text,
    address     inet NOT NULL,
    port        int NOT NULL,
    protocol    text,
    transport   text,
    dns_ok      boolean,
    tcp_state   text,
    tls_state   text,
    udp_state   text,
    rtt_ms      int,
    error       text,
    asn         int,
    country     char(2),
    ip_prefix   cidr
);
CREATE INDEX IF NOT EXISTS block_scan_results_addr ON block_scan_results (address, port);
CREATE INDEX IF NOT EXISTS block_scan_results_scan ON block_scan_results (scan_id);
```

Ретенция сканов: `block_scan_runs` — 12 месяцев, `block_scan_results` — 3 месяца
(сырые срезы нужны для диффов, дальше хватает агрегата).

## 7. Обогащение ASN/страной

- Источник — оффлайн-база MaxMind GeoLite2 **ASN** (файл кладём рядом с существующими
  `deploy/data/*/geo/geoip.dat`, читаем через `maxminddb` один раз при старте,
  перезагрузка по mtime). Никаких внешних HTTP-вызовов в горячем пути: ручка должна
  работать при заблокированном из РФ интернете.
- Если IP приватный/нерезолвимый (например, отчёт пришёл через локальный прокси) →
  `asn = NULL`, `country = NULL`, вердикт считается «неизвестным оператором».

## 8. Агрегация, вердикт, алерты

Воркер раз в 5 минут:

1. Пересчитывает текущий час в `block_report_rollup_hourly`.
2. Для каждой пары (`node_address`, `port`) за последние 30 минут считает
   `distinct_asn` и долю провалов `tcp_state IN ('timeout','refused','err')` / `udp_state IN ('no-answer','err')`.
3. Сверяет с полигоном: `env/node-probe/latest.json` (публикуется раз в 20 минут,
   API читает его локально или через уже существующий source) — «полигон видит адрес
   открытым?».
4. Сверяет со `/status` (`NodeResponse.status`).
5. Ставит вердикт:

| Условие | Вердикт |
|---|---|
| `distinct_asn ≥ 3` за 30 мин и `tcp_fail_ratio ≥ 0.8`, полигон видит адрес **открытым** | `blocked` (блокировка у операторов) |
| нода в `/status` = `Offline` | `node_offline` (не блокировка) |
| полигон видит адрес **закрытым** так же, как юзеры | `local_ok` → значит проблема не у оператора, а у нас/сети |
| данных мало (< 2 отчётов) | `unknown` |

6. Алерт: при первом переходе в `blocked` для пары (`address`,`port`) — событие в
   существующий канал алертов (Prometheus-метрика + уведомление), не чаще одного раза
   в 30 минут на пару.

Метрики: `frkn_block_reports_total{address,port,asn}`, `frkn_block_suspected{address}`,
`frkn_block_report_rejected_total{reason}`.

### 8.1 Сканы: матрица покрытия и диффы

Сканы дают то, чего не даёт одиночный отчёт — **срез по всем адресам с одной точки**.
Обработка при приёме скана (синхронно, до ответа 202 не нужно — можно воркером):

1. Для каждой пары (`address`, `port`) из `block_scan_results` считаем
   `reachable = (tcp_state = 'open' OR udp_state = 'answered')`.
2. Матрица покрытия за 24 часа по (`address`, `port`) × `asn`: сколько точек видят
   адрес открытым, сколько закрытым.
3. **Дифф с предыдущим сканом той же подписки**: адрес, который в прошлом скане был
   `open`, а в этом — `timeout` → событие `node_ip_degraded` (это самый быстрый и
   точный сигнал: один и тот же оператор, тот же телефон, изменился только наш адрес).
4. **Покрытие по ASN**: адрес, открытый у ≥ 2 разных ASN, но закрытый у конкретного —
   вердикт `blocked_for_asn` (локальная блокировка у оператора, нода при этом жива).
5. Сводка в `GET /v1/admin/report/scan?from=&to=&address=` — матрица
   `address:port × asn` с `ok/total`, плюс список деградаций за период.

Алерты сканов: `node_ip_degraded` (адрес перестал открываться у того же клиента) и
`asn_block` (≥ 3 скана от разных подписок одного ASN показывают закрытый адрес при
живом полигоне). Первый — приоритетный: он ловит блокировку конкретного IP нашими же
глазами, без жалобы в поддержку.

## 9. Скоринг (фаза 2, отдельной задачей)

Добавить в `src/score/` сигнал `blocked_asn_ratio` (доля отчётов с вердиктом `blocked`
за сутки по ноде) и серию `node.score.blocked_ratio`. Использовать **только** как
понижающий коэффициент: блокировка у части операторов не должна убирать ноду из
выдачи целиком, если она жива для остальных.

## 10. Приватность

- Не храним: IP пользователя в открытом виде, содержимое трафика, историю посещений.
- Храним: ASN/страну/префикс, `blake3(ip+salt)`, свободный текст пользователя
  (он его пишет сам, осознанно).
- В клиенте отправка — **только по явному действию** (кнопка), без автоотправки.
- В админ-выдаче не показываем `ip_hash` и `user_note` целиком (только по клику на
  конкретный отчёт).

## 11. Этапы и оценка

| Этап | Содержание | Оценка |
|---|---|---|
| Э0 | роут + хендлер + валидация + запись в БД + юнит-тесты на отказы | 0.5 дня |
| Э1 | стор, дедуп, rate limit, ретенция | 1 день |
| Э2 | роллап-воркер, агрегатная админ-ручка, метрики | 1 день |
| Э3 | сверка с полигоном + вердикты + алерт | 0.5–1 день |
| Э4 | `POST /v1/report/scan` + таблицы сканов + матрица покрытия, диффы, админ-сводка | 1.5 дня |
| **Итого** | | **4.5–5 дней** |

Э0–Э3 и Э4 независимы: сканы можно делать после того, как заработает приём одиночных
отчётов, и они не блокируют клиентскую часть (клиент шлёт `mode: current` в ту же
ручку, пока нет полного скана).

## 12. Критерии приёмки

```bash
# валидный отчёт
curl -sS -X POST https://api.frkn.org/v1/report/block -H 'content-type: application/json' -d @report.json
# → 202 {"id":"…","dedup":false}

# повтор того же report_id
# → 202 {"id":"…","dedup":true}, вторая строка в block_reports не появилась

# битый JSON / неизвестный протокол / port=0
# → 400 с указанием поля

# без auth_data или с чужим uuid
# → 401

# 11 запросов за минуту с одного IP
# → 429 на 11-м

# агрегат
curl -sS 'https://api.frkn.org/v1/admin/report/block?from=2026-10-26T00:00:00Z&group_by=asn'
# → строки с reports/distinct_asn/verdict
```

Дополнительно: 3 отчёта с разных ASN по одному адресу и порту при живом полигоне →
в течение 5 минут появляется вердикт `blocked` и событие алерта.

## 13. Что должен доработать клиент (dopamine)

Сейчас `connectionReportController` умеет одно: по кнопке пробить **текущий** сервер и
скопировать отчёт в буфер. Ни отправки, ни полного скана, ни автосбора нет.

### 13.1 Отправка (обязательное, без неё ручка мертва)

1. `report_id` / `scan_id` — uuid4 на каждый сбор.
2. `support_tag` (`apiDevicesModel.cpp:85` — роль уже есть), `error {code, stage}`,
   `device {splittunnel, killswitch, mtu}`.
3. `ApiConfigsController::sendConnectionReport(QJsonObject)` — POST на
   `getGatewayEndpoint() + "v1/report/block"` и `…/v1/report/scan`, `auth_data` по
   образцу `apiConfigsController.cpp:165`.
4. Состояния в UI: `idle / collecting / sending / sent / error(http)`. При `401` —
   «подписка не активна», при `429` — «слишком часто, попробуйте позже».

### 13.2 Полный скан всех серверов

Требование: **проверять каждый адрес каждого сервера**, а не только выбранный.

- Источник списка: `ServersModel` (все конфиги из подписки) + `GET /status`
  (инвентарь нод: адреса и порты). Объединять по паре `address:port`, дубликаты
  схлопывать.
- Для каждого сервера: DNS-резолв хоста → **все** A/AAAA-адреса → для каждого адреса
  каждый порт из его конфига (у AWG это обычно один UDP-порт, у VLESS — 443/tcp
  плюс альтернативные).
- Типы проб по протоколу:
  - `tcp`-порты — TCP connect + опционально TLS-хендшейк (для vless/reality хватает
    TCP connect, TLS отдельно не нужен, SNI может блокироваться — фиксируем как
    отдельное поле только если реально делаем);
  - `udp`-порты — отправить и ждать: «answered / no-answer / closed(err)». Помнить,
    что `no-answer` — не доказательство блокировки (см. §4.4).
- Бюджет: ≤ 64 параллельных проб, таймаут 3 с на пробу, общий потолок 60 с на скан,
  прогресс `{done, total}`, кнопка «Отмена». При 50 серверах × 2 адреса × 2 порта =
  200 проб это ~15–20 с.
- **Запускать только при выключенном VPN** (иначе все пробы идут через туннель и
  показывают задержку туннеля, а не доступность). На десктопе можно дополнительно
  биндить сокеты к физическому интерфейсу, на Android — просто требовать
  отключённого коннекта.

### 13.3 Автосбор по текущему коннекту

Пока туннель поднят, клиент **сам** копит метрики, без действий пользователя:

- раз в 60 с — RTT до текущего сервера (ICMP/TCP-проба через туннель; это метрика
  качества текущего канала, её и надо видеть) и признак успеха;
- буфер 60 точек (час истории), из них `rtt_ms_avg`, `rtt_ms_p95`, `loss_pct`;
- при разрыве/ошибке — фиксируем `disconnect {code, stage, at}` и `uptime_s`;
- данные уходят в поле `session` при следующей отправке отчёта.

Сбор — да, автоматический. **Отправка — по-прежнему по действию пользователя** на
этом этапе: автоотправка телеметрии без согласия — отдельное решение, и его надо
принимать явно (в рисёрче это фаза 2).

### 13.4 UI

В `PageSettingsApiSupport.qml`, рядом с существующим «Connection report»:

- «Connection report» (как сейчас) — текущий сервер, сбор + отправка;
- «Check all servers» — полный скан: прогресс `N/M`, «Отмена», по завершении —
  сводка «открыто X из Y адресов» и кнопка «Отправить в поддержку»;
- состояние отправки и код ответа — в `descriptionText` пункта.

Оценка клиентской части: отправка 0.5 дня, полный скан 1.5 дня, автосбор сессии
0.5 дня, UI 0.5 дня — **≈3 дня**.
