# Локализация имён пулов (pool / pool_translation)

Dopamine показывает локализованное имя группы серверов (пула), которое отдаёт
бэкенд в `/v1/services` вместе с контрактом `pool` / `pool_translation`.
Бэкенд-часть — `ffcore/docs/node-pool.md`.

## Контракт ответа

```json
{
  "connection_label": "Finland · AmneziaWG",
  "pool": "Finland",
  "pool_translation": { "en": "Finland", "ru": "Финляндия", "uk": "Фінляндія" }
}
```

## Приоритет имени (в `ApiConfigsController::decorate()`)

1. перевод пула для текущего языка UI (`en` / `ru` / `uk`; `ua` — алиас `uk`);
2. `pool` (если перевод для языка отсутствует);
3. старое имя, разобранное из `connection_label` (прежнее поведение).

`ServersModel::localizedPoolName(pool, pool_translation, locale)` — единая точка
выбора; используется и в `decorate()`, и в `ServersModel::NameRole`.

## Поведение

- **Пользовательское имя неприкосновенно.** Если сервер переименован
  (`nameOverriddenByUser == true`), имя не переводится ни в `decorate()`, ни в
  `NameRole`.
- **Смена языка без перезапуска.** `NameRole` вычисляется динамически из
  сохранённого `displayInfo.pool` / `displayInfo.poolTranslation`, поэтому при
  `CoreController::translationsUpdated` `ServersModel::onTranslationsUpdated()`
  только рассылает `dataChanged` (и `defaultServerNameChanged` для трея) —
  список/домашний экран/детали/трей перечитывают имя, без релоада подписки.
- **Отсутствие перевода даёт старое имя** (или `pool`, если он задан, но перевода
  для языка нет).
- **VPN-конфиг и ключи не трогаются** — меняется только отображаемое имя.
- **Старый бэкенд** (без `pool`/`pool_translation`) работает как раньше:
  `localizedPoolName` вернёт пустую строку, используется имя из
  `connection_label`.

`pool` / `poolTranslation` кладутся в `displayInfo` сервера и сохраняются при
обновлении (эти поля уже прокидываются в `updateServiceFromGateway` и
`installSubscriptionConfig`).
