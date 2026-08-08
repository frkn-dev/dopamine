# BUG: клиент не может отличить ноды — connection_uuid общий, node_id не отдаётся

## Модель бэка (как объяснили)

`connection_id` — общая сущность для группы нод (например, все AWG-ноды
подписки делят один connection_id). Ноды различаются по `node_id`.

## Проблема

`node_id` **не приходит** в `/v1/services`. Фактический состав коннекшна
(последний ответ из лога, gateway свежей версии):

```json
{
  "connection_label": "BelySlonSuomi 🐘 · VLESS XHTTP CDN",
  "connection_uuid": "b023cacd-...",
  "country_code": "HEL",
  "country_name": "HEL",
  "env": "wl",
  "service_protocol": "vless"
}
```

Поля `node_id` нет. При этом `connection_uuid` продублирован у разных нод —
пример из одного ответа /v1/services (6 AWG-нод, один uuid):

```
uuid=1ac65b82 → 'Netherlands22 · AmneziaWG'
uuid=1ac65b82 → 'Kazakhstan · AmneziaWG'
uuid=1ac65b82 → 'Suomi · AmneziaWG'
uuid=1ac65b82 → 'Suomi2 · AmneziaWG'
uuid=1ac65b82 → 'Swe · AmneziaWG'
uuid=1ac65b82 → 'Uncle Sam · AmneziaWG'
```

## Чем это ломает клиент

Клиент для каждой строки сервера зовёт `/v1/config` с `connection_id` — и
получает конфиг **одной и той же ноды** на все строки (той, кому uuid
реально принадлежит). В приложении Suomi и Suomi2 показывают одинаковые
настройки Suomi2. Различить ноды на клиенте невозможно в принципе.

Побочно: `/v1/config` без точного указания ноды возвращает произвольную —
недетерминированно.

## Что нужно от бэка (одно из двух)

1. **Вариант А:** в `services[].connections[]` добавить `node_id`, и
   `/v1/config` должен принимать `node_id` и возвращать конфиг именно этой
   ноды.
2. **Вариант Б:** сделать `connection_uuid` уникальным per node (тогда
   текущий `connection_id` в `/v1/config` уже работает — клиент его шлёт).

Также просим задокументировать поведение `/v1/config` при неоднозначном
матче (несколько нод на country+protocol): сейчас молча возвращается
произвольная.

## Клиент готов

Клиент уже шлёт `connection_id` при установке, рефреше и реконнекте
(`fetch_frkn_config.py` в репо — ручная проверка). Как только в ответе
появится `node_id` (вариант А), добавим его в payload аналогично.
