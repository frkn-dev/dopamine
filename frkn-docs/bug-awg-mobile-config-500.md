# BUG (v0.6.22): /v1/config для awg-mobile коннекшна падает 500 "Failed to build VLESS config"

## Репро

`/v1/services` отдаёт мобильный коннекшн корректно:

```
conn: proto='awg-mobile' uuid=42ede480-0503-43ff-ab86-17d9aec12960
      node_id=7a66718f-4b7b-4239-8841-d3c1f9054068 env='dev' country=HEL
      label='Suomi2 · AmneziaWG'
```

Но `/v1/config` для него — HTTP 500, расшифрованное тело:

```json
{"status": 500, "message": "Failed to build VLESS config", "response": null}
```

Похоже, config-builder для awg-mobile коннекшна ошибочно идёт по ветке
VLESS и падает.

## Запрос (все варианты service_protocol падают одинаково)

```json
{
  "os_version": "linux", "app_version": "4.8.14.33", "app_language": "ru",
  "installation_uuid": "...",
  "auth_data": {"id": "c6831dad-c6b5-4148-9e45-f7a0353fbdbc"},
  "user_country_code": "RU", "server_country_code": "HEL",
  "service_type": "amnezia-premium",
  "service_protocol": "awg-mobile",   // также пробовали "awg" и "vless" — тот же 500
  "connection_id": "42ede480-0503-43ff-ab86-17d9aec12960",
  "node_id": "7a66718f-4b7b-4239-8841-d3c1f9054068"
}
```

Контроль: обычные awg и vless коннекшны той же подписки — 200 OK.

## Регрессия

До v0.6.22 этот же коннекшн (uuid 42ede480) отдавал валидный AWG-конфиг
(`Address = 10.77.0.2/32`, endpoint 138.124.124.203:8443). Сломано релизом
0.6.22.

## Эффект на клиенте

Мобильный сервер полностью исчезает из списка: клиент скипает коннекшны,
чей /v1/config упал. После фикса бэка на клиенте ничего менять не нужно.
