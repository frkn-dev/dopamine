# Changelog

## 2026-10-19 — список серверов: All и группировка по локациям

- В фильтр протоколов добавлен вариант **All**: EU и RU отображаются одновременно;
  выбор пользователя больше не сбрасывается принудительно на `awg-mobile`.
- Серверы группируются по `subscription_id + pool` с fallback на имя/страну. Одна
  локация представлена одной группой, а не отдельными карточками для каждого протокола.
- В свёрнутом виде показывается предпочтительный вариант (`awg-mobile` → `awg` →
  `hysteria2` → `vless` → `wireguard`), по раскрытию — все варианты. Выбор
  конкретного `connection_uuid + node_id`, health, server info и reconnect работают
  как раньше; записи разных подписок не объединяются.
- `ServersModel::getServerDisplayData()` отдаёт QML исходный index и отображаемые
  поля, поэтому хранение и VPN-логика не меняются.

## 2026-10-17 — локализация имён пулов (pool / pool_translation)

- `ApiConfigsController::decorate()` выбирает имя сервера по приоритету: перевод
  пула для текущего языка UI (`en`/`ru`/`uk`, `ua` — алиас `uk`) → `pool` →
  старое имя из `connection_label`. Бэкенд-контракт `pool` /
  `pool_translation` — `ffcore/docs/node-pool.md`.
- `ServersModel::localizedPoolName()` — общий выбор имени; `NameRole` считает его
  динамически из `displayInfo.pool` / `displayInfo.poolTranslation`.
- Смена языка не требует перезапуска: `translationsUpdated` →
  `ServersModel::onTranslationsUpdated()` рассылает `dataChanged` и
  `defaultServerNameChanged` (список/дома/детали/трей).
- Пользовательское переименование (`nameOverriddenByUser`) всегда приоритетно и
  не переводится; при отсутствии перевода используется старое имя; VPN-конфиг и
  ключи не меняются. Подробности — `frkn-docs/pool-localization.md`.

## 2026-10-17 — подписка: обновление нескольких подписок за раз

- `reloadSubscriptionConfigs()` больше не берёт одну подписку
  (`resolveSubscriptionId()`) и не удаляет «лишние» серверы глобально: он собирает
  все FRKN subscription id из сохранённых серверов (`api_config.auth_data.api_key`,
  fallback — legacy `.id`) и обновляет их последовательно и независимо.
  Подробности — `frkn-docs/subscription-multi-refresh.md`.
- Ключ сопоставления/удаления — `subscription_id + connection_uuid + node_id`:
  одинаковые `connection_uuid`/`node_id` в разных подписках больше не перезаписывают
  друг друга.
- При рефреше подписки A удаляются только её записи, реально исчезнувшие из ответа;
  ошибка или пустой ответ одной подписки не удаляет и не блокирует остальные.
- Non-FRKN/foreign, share-записи и записи без известного владельца полностью
  пропускаются (не обновляются и не удаляются).
- Fetch вынесен в `fetchSubscriptionFeed` с локальными буферами — общие поля
  контроллера (`m_subscriptionConfigs`/`m_subscriptionConfigFailures`) больше не
  смешивают результаты разных подписок (они остались только для одиночного импорта).
- Дедуп при импорте страны (`ServersModel::isServerFromApiAlreadyExists`) теперь
  учитывает subscription id: одинаковые country/service/protocol в другой подписке
  больше не считаются дубликатом.

## 2026-09-26 — split presets: named builtins + API union

- Builtin каталог сервисов с теми же id, что API: `youtube`, `instagram`,
  `tiktok`, `x`, `facebook`, `whatsapp`, `telegram`, `netflix`, `spotify`,
  `discord` (+ по-прежнему `builtin-ru-*`, `builtin-ai`).
- Merge: `domains = unique(builtin ∪ API)` по `id` (`BuiltinSplitPresets::mergeWithApi`);
  кэш на диске — только ответ API.
- Дока для бэка: `frkn-docs/split-presets-backend.md` (в т.ч. удалить `twinby`).

## 2026-07-29 — 4.8.14 (22–23), TestFlight iOS + macOS

### Серверы и подписка

- Фильтры над списком серверов — два дропдауна рядом (`FilterDropDown.qml`):
  протокол (Все / AmneziaWG / Hysteria2 / VLESS / WireGuard) и env
  (White Elephants = `wl`, Regular = `dev`, Reverse = `ru`; остальные значения — как есть).
  Выбор персистится (`Conf/serversProtocolFilter`, `Conf/serversEnvFilter`).
  Дефолт протокола — AmneziaWG (если есть в подписке), env — Все.
  Дропдаун виден только когда вариантов больше одного.
- `env` парсится из connection'ов `/v1/services` (gateway v0.6.11) в `apiConfig.env`;
  в ServersModel роль `connectionEnv` + `availableEnvs`.
- Подписи серверов: заголовок — только имя ноды, мелкая строка — детали протокола + IP
  («VLESS TCP Reality · 91.199.137.92», «AmneziaWG · 45.136.175.27»).
  `connection_label` от бэка («Имя · Детали») разбивается по « · ».
- Флаги стран в списке серверов: роль `countryCode` в ServersModel
  (`server_country_code` → `user_country_code` → `displayInfo.countryCode`),
  SVG из `qrc:/countriesFlags/images/flagKit/<CC>.svg`.
  Алиасы не-ISO кодов бэка в `CountryCodeRole`: `SWE→SE`, `HEL→FI`, `UK→GB`, `EST/TLL/EESTI/ESTONIA/TALLINN→EE`.
  При отсутствии файла флаг скрыт (без ошибок в лог).
- Кнопка «Reload all servers from subscription» (Настройки → Application):
  подтверждение-шторка, запрет при активном подключении, busy-индикатор.
  `ApiConfigsController::reloadSubscriptionConfigs()`: subscription id восстанавливается
  из импортированного сервера, заново тянутся `/v1/services` + `/v1/config`,
  старые серверы подписки (с `connection_uuid`) заменяются свежими.
  Серверы, добавленные вручную (ключ/QR), не трогаются. Переводы en/ru/uk.
- Plain WireGuard в подписке: `service_protocol: "wireguard"` — клиент генерирует
  WG-ключи и шлёт `public_key` в `/v1/config` (раньше только для `awg`).
  Спека для бэка: контейнер **`amnezia-wireguard`** (не `amnezia-awg`),
  `config` — INI-строка wg-quick, `last_config` — JSON
  (`client_priv_key`, `client_pub_key`, `server_pub_key`, `psk_key`, `client_ip`,
  `port`, `hostName`, `mtu`, `persistent_keep_alive`), `isThirdPartyConfig: true`.
  Junk-параметры (AWG-специфика) не нужны.

### UI

- Главный экран: на свёрнутой карточке только имя сервера — строка с протоколом/IP
  убрана (IP остался в шторке и в списке серверов); геометрия карточки сохранена,
  название опущено для пропорций (topMargin 26 в collapsed).
- Заголовок «Servers» в шторке убран.
- Птеродактиль (`client/images/pterodactyl.png`, вырезан из launch.png):
  при успешном подключении взлетает из центра экрана и садится в круглую кнопку,
  остаётся полупрозрачным (0.22) фоном под надписью, пока соединение активно;
  гаснет при отключении. При автоконнекте/восстановлении состояния — сразу в кнопке,
  без анимации. Логика в PageHome.qml + `birdPerched` в ConnectButton.qml.
- Надпись Connected — терминальный зелёный `#00FF41` (`connectedTextColor` в ConnectButton.qml).

### Платформы

- Все изменения — общий QML/C++ код, платформенно-независимые; на Android должны
  работать без доработок, но не проверялись (нет устройства).
- 4.8.14 (22) и (23) залиты в TestFlight: iOS + macOS. Пайплайн — frkn-docs/ios.md, frkn-docs/mac.md.
- Спека plain WireGuard для бэка — frkn-docs/api-wireguard.md.

## Backlog (запланировано, не в релизе)

- **Размер приложения** (сейчас 85 МБ установленного: 45 основной бинарник Qt-static,
  30 network extension, 9 OpenVPNAdapter):
  - стрип Go-бинарников (`-ldflags "-s -w"` при сборке xray/wg через gomobile) — минус ~20–30% NE;
  - выпилить OpenVPNAdapter.framework + OpenVPN/Cloak/ShadowSocks-код апстрима — протокол
    не используем, минус ~9 МБ; вместо него позже — **TrustTunnel**.
- **Perf-проход «Формула-1»**: время старта, ленивая загрузка QML-страниц, аудит
  линкуемых Qt-модулей (лишнее из static-сборки), проверка strip в Release.
- **TrustTunnel**: спека + оценка интеграции (кандидат вместо OpenVPN-пайплайна).
  Открытый протокол AdGuard VPN (Apache 2.0, янв 2026, github.com/TrustTunnel/TrustTunnel),
  HTTPS-маскировка против DPI. Рисерч: язык ядра, embeddable-библиотека для NE, объём работ.
- **Сплит-туннелинг пресеты сервисов**: галочки «YouTube / ChatGPT / Gemini / Instagram…» —
  каждая добавляет пачку доменов сервиса разом (у одного сервиса много доменов, юзер не
  должен их перечислять). **Списки пресетов отдавать динамически с бэка** (endpoint через
  AGW-конверт, как новости) — менять без ребилда приложения; в клиенте — кэш на офлайн.
- **Android**: собрать и прогнать на устройстве (когда будет) — весь новый UI общий QML.
- **Latency-тест серверов** (отложено по решению): TCP-probe для VLESS, WG-handshake-probe,
  QUIC-probe для H2; отображение «N ms» в списке.
