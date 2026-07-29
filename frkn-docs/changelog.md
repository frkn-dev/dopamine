# Changelog

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
  Алиасы не-ISO кодов бэка: `SWE→SE`, `HEL→FI`, `UK→GB` (`flagCountryCode()` в ServersListView.qml).
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
