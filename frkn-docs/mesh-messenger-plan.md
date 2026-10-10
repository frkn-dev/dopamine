# Mesh-мессенджер: техплан MVP («написать Привет»)

> **2026-10-07 — актуальный статус:** работа перенесена на ветку `feature/mesh`
> (от свежего `dev`). Старая `feature/mesh-messenger` — архив, не мёржить.
> Текущая архитектура — раздел «FRKN Mesh MVP (feature/mesh)» ниже; дальше
> идёт исходный план, оставлен как история.

## FRKN Mesh MVP (feature/mesh)

Мессенджер последней мили внутри Dopamine: текст, когда интернет есть и
когда его нет. Контакт = числовой UIN (как ICQ), без ФИО/почты/телефона.

Два транспорта, один UI (`PageMeshChat`):

- **BLE mesh** (BitChat core, vendored): пир в радиусе → 1:1 по identity
  fingerprint (Noise-сессию ядро поднимает само).
- **fcore-релей** (модуль `/v1/mesh` в fcore): пир вне радиуса, но есть
  интернет → односторонний Noise X конверт (пролог `bitchat-courier-v1`,
  те же функции sealCourierPayload/openCourierPayload, что для курьерских
  конвертов в ядре). Сервер видит только ciphertext и UIN'ы; истории на
  сервере нет — слот живёт до pickup/TTL и стирается при pull.

Идентичность:

- Noise static keypair (Curve25519) + Ed25519 signing keypair на устройстве
  (переиспользованы ключи BitChat identity).
- fingerprint = lowercase hex SHA256 от raw static pubkey — внутренний ключ
  BLE, в UI не показывается.
- UIN выдаёт fcore при `POST /v1/mesh/register` (идемпотентно по pubkey),
  хранится в `Conf/meshUin`.
- Авторизация send/pull — Ed25519-подпись каноничной строки
  (`send\n{to}\n{ts}\n{ciphertext}` / `pull\n{uin}\n{ts}`), ключ проверки
  сервер знает из register.

fcore API (`/v1/mesh`): `register`, `lookup/{uin}` (uin, online, pubkey),
`send` (только ciphertext), `pull` (забрать и стереть). Онлайн = last_seen
за 5 минут (pull/register/send обновляют).

Клиентская склейка (`MeshChatController` + `MeshNetClient`): пир рядом
(fingerprint в BLE-снапшотах) → BLE; иначе есть сеть → seal + send; нет ни
того ни другого → «не доставлено». История — только локально
(`AppDataLocation/mesh-history.json`, до 200 сообщений на контакт).
Pull — таймер 15 с, пока открыт чат.

### Журнал (feature/mesh)

| Дата | Что и куда |
|---|---|
| 2026-10-07 | Перенос на свежий dev: вендоринг `client/3rd/bitchat-ios/` и донорские MeshBridge/meshBridgeWrapper/meshChatController/PageMeshChat извлечены из `refs/donor/mesh-messenger` (f6122925); glue написан заново на dev (ios.cmake, Info.plist.in, PageEnum/qrc, coreController, settingsController, settings.h, PageSettingsApplication, переводы en/ru/uk). UIN-модель: MeshBridge + bridge-обёртки расширены myPubkey/mySigningPubkey/sign/sealTo/unseal; новый `MeshNetClient` (fcore `/v1/mesh`, подписи Ed25519); `MeshChatController` переписан (UIN-контакты, склейка BLE/релей, локальная история, pull-таймер); в BLEService добавлены узкие обёртки noiseSealPayload/noiseOpenSealedPayload. Android: модуль `client/android/mesh/` (вендоринг bitchat-android 6803d6d, BLE core без Wi-Fi Aware/Nostr/geohash/media), Noise X courier портирован в noise/NoiseEncryptionService.kt (совместимость с iOS проверена кросс-тестом в обе стороны), фасад MeshBridge.kt, C++ шов platforms/android/meshBridgeWrapper (тот же интерфейс, что iOS), пермишены в AndroidManifest. fcore: модуль `src/mesh/` + хендлеры `/v1/mesh/*` (register/lookup/send/pull, sealed-слоты в памяти TTL 7д, реестр в rkyv-снапшоте), 8 тестов модуля зелёные. Проверки: macOS-сборка C++ зелёная; `:mesh:assembleDebug` зелёный; iOS Swift и полный APK не собирались (нет места/Qt iOS на машине) — прогнать при первом билде на устройства. |
| 2026-10-09 | Клиент под отдельный meshd (бинарь в fcore, не в этом репо): `Conf/meshApiBase` по умолчанию `https://mesh.frkn.app/v1/mesh`; register шлёт `subscription_id` и тот же UUID как `subscription_secret` (в api секрет подписки — сам id из `/sub?id=`); send кладёт по конверту на каждый `device_pubkey` из lookup; pull-таймер заменён long-poll без transfer timeout, ошибка сети — пауза 5 с. Своё имя — поле рядом с UIN, `POST /name`. Pet name — локальное поле `name` контакта (долгий тап в списке и в шапке переписки), на сервер не уходит. Смена подписки вызывает повторный register; при 409 UIN не сбрасывается (ключ уже привязан к другой подписке). |
| 2026-10-09 | iOS-ядро перевендорено. Коммита `6803d6d` в `permissionlesstech/bitchat` нет — это SHA `bitchat-android` (2026-10-06, «Bump Wear release»). Ближайший по дате коммит iOS-репо: `5e9287fae1` (2026-09-24, «Keep composer drafts…», HEAD `main`; коммитов после 2026-09-24 нет). Расхождение с Android-вендором — 12 дней, протокол может ещё разъехаться. Перенесено с прошлого вендора (`f0249d9` + локальные патчи): стабы `Nostr/NostrIdentity.swift`, `NostrIdentityBridge.swift`, `NostrEvent.swift` (полный Nostr и secp256k1 не брали); шим `BitchatApp.swift` (только `bundleID` для KeychainManager); `Services/BoundedIDSet.swift` оставлен на прежнем пути — у апстрима файл уехал в `Services/Gateway/`, содержимое то же, Gateway не вендорим; в `BLEService.initializeBluetoothManagersIfNeeded` снова сняты restoration identifiers (без background modes CoreBluetooth падает); узкие обёртки `noiseSealPayload` / `noiseOpenSealedPayload`; `VerificationService.verifyScannedQR` — `Swift.abs` вместо перегруженного `abs` (иначе не собирается в таргете приложения); срезаны `import BitFoundation` / `import BitLogger`, включая `import struct BitFoundation.BitchatPacket` в `PacketIdUtil.swift`; `private import CryptoKit/Compression` в BitFoundation нормализованы в обычный `import` (один таргет приложения, иначе ambiguous access level). Добавлен новый файл ядра `PacketPayloadLimits.swift` (его зовёт сборка фрагментов). `Utils/MessageDeduplicator.swift` взят из `5e9287fae1`: на `f0249d9` файл был дубликатом InputValidator и мы держали свой класс, на этом коммите апстрим вернул настоящий тип с тем же API (`markProcessed`/`isDuplicate`/`contains`/`reset`/`cleanup`), свой заменитель больше не нужен. `platforms/ios/MeshBridge.swift` не трогали. Android `client/android/mesh` не трогали: `BluetoothPermissionManager.hasBluetoothPermissions` по-прежнему на API 31+ проверяет только `BLUETOOTH_ADVERTISE/CONNECT/SCAN` и не требует location (`neverForLocation`). Сборка: cmake configure `build-ios` + `xcodebuild` Release зелёная. На iPhone приложение установлено; запуск отклонён — экран заблокирован. Android не пересобирался. |
| 2026-10-07 | Первый прогон на устройствах: iOS падал при открытии чата — `BLEService.initializeBluetoothManagersIfNeeded` передавал restoration identifiers, а CoreBluetooth требует для них background modes bluetooth-central/peripheral, которые мы сознательно не запрашиваем (foreground-only MVP) → убраны (точечная правка вендоренного BLEService.swift, переносить при обновлении из апстрима). Вход в чат продублирован иконкой на главной (PageHome, слева вверху, за флагом, иконка `images/controls/chat.svg`). Билды: iOS Release через Xcode 26.1.1 (`DEVELOPER_DIR=...`, xcode-select указывает на старый Xcode 18.1 без device-платформы) — установлено на устройство через devicectl; Android release APK подписан локальным тестовым keystore `deploy/build/mesh-local.keystore` → `deploy/build/Dopamine-4.8.14.66-MESH-TEST-arm64-v8a.apk`. Грабли: `build_android.sh` требует gnu-getopt в PATH; sparse-checkout 3rd-prebuilt должен включать xray (libxray.aar, HevSocks5Tunnel.xcframework), wireguard/{macos,ios,android}, openssl/{macos,ios,android}, libssh/{macos,ios}, amnezia_xray/macos — на записанном коммите submodules, не на tip. |

| 2026-10-10 | Приоритет транспортов перевёрнут на relay-first: при сети 1:1 идёт через meshd (подтверждённая доставка), BLE — только офлайн/нет pubkey (в meshChatController::sendMessage). Причина: остаточный баг приёма BLE 1:1 на iOS (Android постит NOISE_ENCRYPTED ok=true, iPhone молча не показывает; BLE receive hook didReceiveNoisePayload добавлен в MeshBridge и чинил часть, остаток — в бэклоге с диагностикой приёмника). Доставка в фоне: foreground-only по решению юзера (2026-10-10) — poll и BLE глушатся на Hidden/Suspended/aboutToQuit (иначе iOS нудит «accessory would like to open» при смерти/уходе приложения с живой BLE-сессией); poll-петля получила watchdog 40с (подвисший сокет при скачке сети раньше убивал петлю навсегда — m_pollInFlight залипал). Кандидаты на будущее фона: Android foreground service, APNs push с meshd (ключ уже в репо), iOS background BLE modes. |

## Исходный план (архив, feature/mesh-messenger)

Ветка: `feature/mesh-messenger`. Вся работа — только здесь, `dev` не трогаем.
Журнал изменений — в конце этого файла (что, когда, куда).

Референс: [permissionlesstech/bitchat](https://github.com/permissionlesstech/bitchat)
(iOS/macOS, Swift) + `bitchat-android` (Kotlin, протокол-совместим).
Лицензия Unlicense (public domain) — код можно забирать.

## Базовый минимум: «открыть FRKN и написать Привет»

Сценарий MVP: публичный mesh-канал — любой, у кого открыт экран чата и кто
в BLE-радиусе (до 7 хопов), видит сообщение. Без контактов, без 1:1, без
фона, без истории. Это и есть самое дешёвое доказательство ниши.

### Что для этого нужно

**1. Mesh-ядро из BitChat (не пишем сами):**

iOS (Swift), из `bitchat/Services/BLE/` (~800К, но ядро меньше):
- `BLEService` — центральный сервис: discovery, подключения, ретрансляция;
- `BLEFragmentHandler` / `BLEOutboundFragmentPlanner` — фрагментация под BLE MTU;
- `BLEConnectionScheduler`, `BLEFanoutSelector` — топология;
- `MessageDeduplicationService` — защита от петель ретрансляции;
- пакеты: `bitchat/Protocols/BitchatProtocol.swift`, `Packets.swift`;
- identity: `bitchat/Identity/` + `KeychainManager` (ключевая пара на устройстве).

Выкидываем: Nostr-транспорт, Geohash-каналы, Board/Groups, медиа/голос,
Cashu, команды, уведомления. Для публичного канала Noise-сессии не нужны
(широковещание нешифрованное) — подключаем на 1:1 позже.

Android (Kotlin): то же из `bitchat-android` — `mesh/` пакет (BLE core) +
пакетная модель. Протокол тот же — iOS и Android FRKN увидят друг друга.

**2. Мост в наш Qt-стек:**
- iOS: тонкий ObjC++ wrapper по образцу `ios_controller_wrapper`
  (`meshStart()`, `meshSend(text)`, сигнал `meshMessageReceived(sender, text, ts)`);
- Android: JNI-мост к Kotlin-сервису (по образцу существующих android-мостов);
- C++ `MeshChatController` (модель сообщений для QML).

**3. UI (QML):**
- одна страница `PageMeshChat.qml`: список сообщений + поле ввода + счётчик
  «устройств рядом»; вход — кнопка на главной или в меню;
- за фиче-флагом в настройках (по умолчанию выключено).

**4. Разрешения/платформа:**
- iOS: `NSBluetoothAlwaysUsageDescription` в Info.plist; foreground-only —
  background modes НЕ запрашиваем на MVP (меньше вопросов App Review);
- Android: `BLUETOOTH_ADVERTISE/CONNECT/SCAN` (+ location на старых API).

### Оценка MVP (1 разработчик)

| Блок | Оценка |
|---|---|
| Выдёргивание и обрезка BLE-ядра iOS | 4–6 дн |
| Мост ObjC++ + MeshChatController | 2–3 дн |
| QML-экран чата | 2–3 дн |
| Android: обрезка mesh-core + JNI | 5–8 дн |
| Интеграция, прогон двух устройств, баги | 4–6 дн |
| **Итого** | **≈3–4 недели** |

Известные неизвестные, которые могут растянуть: BLE-ядро может тянуть за
собой куски их Service-графа (решается stub-ами), и Android BLE fragmentation
на железе разных вендоров.

### Что осознанно НЕ в MVP (и цена добавления)

- 1:1 приватные чаты (Noise-сессии) — +1–2 нед;
- фон (iOS background modes) — +1–2 нед + обоснование в App Review;
- история/персистентность сообщений — +2–3 дн;
- аудит крипто/протокола перед публичным релизом — отдельно, обязательно.

## Трекинг изменений

Правило: любые правки по этой теме — только в ветке `feature/mesh-messenger`,
коммиты с префиксом `mesh:`, каждая порция отражается в журнале ниже.

### Журнал

| Дата | Коммит | Что и куда |
|---|---|---|
| 2026-08-09 | (этот файл) | План создан: `frkn-docs/mesh-messenger-plan.md`, ветка `feature/mesh-messenger` от `dev` |
| 2026-08-09 | mesh: vendored core + bridge (первый зелёный билд) | Вендоринг ядра: upstream `f0249d9` → `client/3rd/bitchat-ios/` (BLE/, Protocols/, Noise/, Identity/, Models/, Sync/, Courier/, Prekeys/, часть Services/, localPackages BitFoundation+BitLogger, тесты выкинуты). Выброшено: Nostr/* (кроме стабов `NostrIdentity`/`NostrIdentityBridge`/`NostrEvent`), BoardManager/BoardAlertsModel/UnifiedNotices, внешний пакет secp256k1. Срезаны `import BitFoundation/BitLogger`, нормализованы `private import`. Собственный `Utils/MessageDeduplicator.swift` (у апстрима тип потерян — файл содержит дубликат InputValidator; при ребейзе сверить!). Фасад `platforms/ios/MeshBridge.swift` (C++ interop, колбэки через opaque pointer), мост `meshBridgeWrapper.{h,mm}`, `ui/controllers/meshChatController.{h,cpp}` (стаб на не-iOS), QML `Pages2/PageMeshChat.qml`, вход в PageSettingsApplication за флагом `Conf/meshChatEnabled` (settings + SettingsController), `PageMeshChat` в PageEnum + qrc, cmake: CoreBluetooth + GLOB swift + `meshBridgeWrapper.mm` в `client/cmake/ios.cmake`, Bluetooth-строка в `client/ios/app/Info.plist.in`. Грабли сборки: FW_CACHE path к старому SDK (чистить FW_* при смене xcode-select), stale Dopamine-Swift.h, C++ interop не экспортирует @convention(c) параметры |
| 2026-08-09 | mesh: 1:1 по ID + аська-модель | ICQ-модель: стабильный ID = fingerprint Noise-ключа устройства (`noiseIdentityFingerprint`). MeshBridge: `myId/peersJson/sendToFingerprint` (личка через `sendPrivateMessage`, ядро само очередит и поднимает Noise-сессию), приём приватных через `didReceiveMessage(isPrivate)` → колбэк с fingerprint отправителя. Контроллер: контакты в `Conf/meshContacts` (JSON), фильтрация по выбранному контакту, pseudo-contact «broadcast», `peerUnreachable`. QML: мой ID с копированием, чипы контактов (reachable/off), добавление по ID, метка «not delivered». Вне зоны → сообщение помечается недоставленным + нотификация (по требованию UX: «если недоступен — пока просто пишем») |

## Про Джека Дорси и сотрудничество

Реалистичная оценка:

- **Напрямую к Джеку** — шанс ответа низкий, но не нулевой: он публично активен
  по BitChat на X (@jack), отвечает на конкретные технические посты. Холодный
  DM/письмо — почти наверняка мимо.
- **Проект ведёт коллектив permissionlesstech** — вот к ним связаться реально:
  issues/PR в репозиториях читают. PR с фиксом или улучшением протокола —
  самый рабочий «вход».
- **Рабочая стратегия контакта**: сначала публичная польза (PR, репорты о
  поведении mesh в реальных блэкаутах РФ/Иран — это их любимая метрика), потом
  предложение. Питч, который может зацепить Джека: «FRKN — VPN с живой
  аудиторией в странах с блэкаутами; встроили ваш mesh как запасной канал;
  вот телеметрия/кейсы». Именно такие кейсы он репостит.
- **Предостережение по бренду**: у BitChat были публичные уязвимости и
  тейкдаун в Индии — сотрудничество афишировать только после нашего аудита
  кода, который заберём.
