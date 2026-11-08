# connect-event: успешное подключение (клиент → api)

Событие «клиент подключился» для аналитики и поддержки. Уходит **тем же**
запросом, что и обычное обновление конфига: `POST /v1/config`. Отдельного
эндпойнта нет и не нужно — запрос и так делается при подключении.

## Когда отправляется

- клиент **успешно** подключился (состояние `Connected`, туннель поднят);
- строка сервера пришла из API (`config_version == AmneziaGateway`), не
  самопальный сервер и не шара;
- включён тумблер телеметрии (`Conf/telemetryEnabled`); при выключенном —
  событие **не отправляется вообще** (ни запроса, ни полей);
- троттлинг на клиенте: один и тот же `endpoint:port` — не чаще раза в 60 с
  (реконнект-цикл не спамит).

Провалы подключения этим механизмом не отправляются — для них есть
`v1/report/block` (автоотчёт при ошибке/тихом обрыве, не чаще раза в 10 минут).

## Запрос

```http
POST /v1/config
Content-Type: application/json
```

```jsonc
{
  "auth_data": {                      // как в обычном /v1/config
    "id": "<subscription/installation id>",
    "api_key": "<...>"
  },
  "is_connect_event": true,

  "connection_id": "504921fc-7201-4b5d-a40c-808570923bd3",  // connection_uuid
  "node_id": "c5c77c34-062f-44e8-8b1c-091cd57729d8",
  "service_protocol": "awg-mobile",   // awg | awg-mobile | wireguard | vless | hysteria2

  "endpoint": "93.89.220.49",         // НОВОЕ: IP, который реально набрали
  "endpoint_port": 443,               // НОВОЕ: порт (у пула у каждого адреса свой)
  "support_tag": "023d804b-e677-446e-aba4-d02742fb0e3a", // НОВОЕ: installation uuid
  "app_version": "4.8.14.79",
  "app_language": "ru"
}
```

Ответ — обычный ответ `/v1/config` (клиент его использует как обычно, событие
на него не влияет).

## Поля

| Поле | Тип | Обяз. | Что это |
|---|---|---|---|
| `is_connect_event` | bool | да | признак события; без него запрос = обычный refresh конфига |
| `auth_data` | object | да | существующий блок аутентификации подписки |
| `connection_id` | string(uuid) | нет | `connection_uuid` соединения — идентичность строки |
| `node_id` | string(uuid) | нет | конкретная нода (у пула — представительная на момент запроса) |
| `service_protocol` | string | да | протокол из `containers[].<proto>` |
| `endpoint` | string(ip) | нет | адрес, на который реально пошёл туннель |
| `endpoint_port` | int | нет | порт этого адреса (`node_ports[ip]`, иначе порт протокола) |
| `support_tag` | string(uuid) | нет | installation uuid; тот же, что в отчётах `v1/report/block` |
| `app_version` | string | да | версия клиента |
| `app_language` | string | да | `ru` / `en` / `uk` |

Все новые поля опциональны: `Option<T>` + `skip_serializing_if` на стороне api,
чтобы старые клиенты (без `endpoint`/`endpoint_port`/`support_tag`) писали
событие с `NULL` в этих колонках, а новый клиент со старым api просто
игнорировался serde'ом.

## Что писать на стороне api

Одна строка на событие, таблица вида `connect_events`:

```
id            bigserial primary key
at            timestamptz not null default now()
subscription_id  uuid          -- резолвится из auth_data
node_id       uuid null
connection_id uuid null
protocol      text
endpoint      inet null
endpoint_port int null
support_tag   uuid null
app_version   text
platform      text             -- ios | android | macos | windows
country       text null        -- по endpoint (GeoIP уже есть в api)
```

Индексы: `(subscription_id, at desc)`, `(support_tag, at desc)`,
`(endpoint, at desc)` — последние два нужны, чтобы по жалобе найти историю
подключений конкретной установки/адреса.

## Зачем

1. **Поддержка**: по `support_tag` видно, куда и когда человек подключался,
   и это склеивается с его отчётом/фидбеком.
2. **Качество**: по `endpoint`+`endpoint_port` видно распределение реальных
   подключений по адресам пула — какие IP не используются или отваливаются.
3. **Совместимость**: `subscription_id + node_id + protocol` уже были в запросе,
   новые поля только добавляют точность.

## Приватность

- ничего, кроме перечисленного: ни публичного IP, ни трафика, ни доменов;
- `support_tag` — случайный uuid установки (не аккаунт, не e-mail), уже
  отправляется в отчётах;
- при выключенном тумблере телеметрии событие не уходит совсем.
