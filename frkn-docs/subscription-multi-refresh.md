# Подписка: обновление нескольких подписок

Клиент хранит все серверы в одном списке (`Servers/serversList`) и не имеет
отдельной сущности «подписка». Пользователь может импортировать несколько
FRKN-подписок — их серверы лежат рядом, и рефреш одной не должен трогать другую.

## Проблема (было)

`ApiConfigsController::reloadSubscriptionConfigs()` брал один subscription id
(`resolveSubscriptionId()` — первый найденный), тянул `/v1/services`, а затем
удалял **все** серверы с `connection_uuid`, которых нет в ответе. Если в клиенте
две подписки A и B, рефреш A удалял серверы B (их нет в ответе A).

`installSubscriptionConfig()` матчил существующий сервер только по
`connection_uuid + node_id` — одинаковые пары в разных подписках перезаписывали
друг друга.

## Как сейчас

1. **Сбор id подписок.** `collectSubscriptionIds()` проходит по сохранённым
   серверам и берёт `api_config.auth_data.api_key` (fallback — legacy `.id`),
   плюс явно введённый `m_subscriptionId`. Дубликаты убираются.

2. **Признак «сервер подписки»** — `isFrknSubscriptionServer(server)`:
   `config_version` пустой или `AmneziaGateway`, непустой `connection_uuid`,
   не share (`shared == true` или `auth_data.share_token`). Так отсекаются
   legacy self-hosted, Telegram-серверы, share-импорты и записи с непонятным
   владельцем — их рефреш не трогает.

3. **Последовательный рефреш.** `reloadSubscriptionConfigs()` обходит id по
   одному; для каждого вызывается `fetchSubscriptionFeed(id, cb, reportErrors)`,
   который тянет `/v1/services` + `/v1/config` и отдаёт результат в **локальные
   буферы**, не трогая `m_subscriptionConfigs` / `m_subscriptionConfigFailures`
   (те остались только для одиночного импорта).

4. **Реконсиляция по подписке** (`reconcileSubscription`):
   - fresh keys = `connection_uuid + node_id` из свежего ответа;
   - удаляются только серверы **этой** подписки, чей ключ не в fresh и не в
     failures (то есть реально исчез из ответа);
   - затем ставятся свежие конфиги.

5. **Ключ матчинга и дедупа.** `installSubscriptionConfigObject()` и
   `ServersModel::isServerFromApiAlreadyExists(country, service, proto, subId)`
   учитывают subscription id: одинаковые `connection_uuid`/`node_id` (или
   country+service+protocol) в разных подписках — это разные серверы.

6. **Изоляция ошибок.** Если подписка A упала или ответила пустым
   `/v1/services`, её старые конфиги не удаляются и не меняются; остальные
   подписки обновляются дальше. Для фонового рефреша — `reportErrors=false`.

## Что не меняется

- Ручные серверы (ключ/QR), share-импорты и foreign-записи рефреш не трогает.
- Одиночный импорт по UUID / `frkn://sub/` / activation key работает как раньше:
  `fetchSubscriptionConfigs()` + `installAllSubscriptionConfigs()`.
- Произвольные HTTP-подписки не рефрешатся (у них нет сохранённых source URL и
  identity).

## Приёмка (как проверять)

- FRKN A + FRKN B + foreign: после refresh/restart остаются все.
- A обновилась, B упала: A изменилась, B и foreign побайтово не изменились.
- Одинаковые `connection_uuid` + `node_id` в разных подписках не перезаписывают
  друг друга.
- Ошибка `/v1/config` сохраняет старый конфиг соответствующей ноды.
- Одиночная подписка продолжает работать без миграции.
