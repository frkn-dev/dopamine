# Dopamine by FRKN — AGENTS.md

VPN-клиент (форк AmneziaVPN): Qt6/QML, C++20. Платформы: macOS (arm64/intel), iOS, Android, Windows.
Протоколы: AmneziaWG (основной, v3.1+ с I1–I5/junk-пакетами), Xray/VLESS. OpenVPN и SSH-серверы выпилены — не воскрешать без явного запроса.

## С чего начать

1. `HUMANS.md` — что делаем сейчас и что застряло.
2. Этот файл — как работаем.
3. Дальше только то, что нужно задаче: `frkn-docs/` (API: подписки, share-токены, devices, IAP), `deploy/BUILD_RELEASE.md` (релизный процесс). Не читать всё подряд.

Версию не угадывать: единственный источник — `set(DOPAMINE_VERSION ...)` в `CMakeLists.txt`.

## HUMANS.md — статус работы

В корне репозитория есть `HUMANS.md` — человекочитаемый статус: ветка, текущая задача, что сделано, что дальше, блокеры.

- В начале задачи обнови `Current`: ветка, задача, статус, дата.
- В конце задачи добавь пункт в `Done` (дата, короткое описание, commit hash если есть) и обнови `Next`.
- `Done` держи коротким: последние 5–10 пунктов; полная история — в git.
- `HUMANS.md` — это «что делаем сейчас», `AGENTS.md` — «как работаем». Не дублируй правила.
- Не пиши секреты: токены, пароли, приватные ключи.
- Для кросс-репо задач используй одинаковый короткий Task ID и ссылки на другие `HUMANS.md`.
- Если задача заблокирована — зафиксируй это в `Blockers / Notes`, а не только в чате.

## Навигация по репо

- `client/` — приложение (QML UI в `client/ui/`, платформенный код в `client/platforms/`, демон в `client/daemon/`)
- `service/` — системный сервис (Windows/macOS privileged helper)
- `ipc/` — IPC между приложением и сервисом (.rep, Qt RemoteObjects)
- `deploy/` — билд-скрипты всех платформ + `BUILD_RELEASE.md` (релизный процесс, схема версий)
- `client/3rd-prebuilt/` — пребилды (tunnel.dll и пр.) — в гите, не пересобирать без необходимости
- `client/translations/` — локализация, см. раздел ниже
- `frkn-docs/` — внутренняя документация по API и протоколам (актуальное); `docs/` — легаси-диаграммы OpenVPN, не читать для текущих задач

## Локализация

- Языки: **только en / ru / uk**. Референс — английский, от него обновлять ru и uk.
- Слоган «Privacy is our Religion» **не переводится** ни на один язык.
- Протокол «AmneziaWG» в названиях остаётся как есть; остальные упоминания Amnezia из UI вычищены — не возвращать.

## Версионирование

Схема **major.minor.patch.tweak** (например 4.8.14.54):
- Apple marketing version = первые 3 компонента (4.8.14), CFBundleVersion = tweak (54)
- Windows MSI = major.minor.tweak (4.8.54)
- Android versionName = полная версия, versionCode растёт монотонно всегда
- **После бампа версии iOS-сборка через Xcode требует реконфигура cmake**, иначе версия зашита старая
- **После добавления/удаления cpp/h-файлов тоже нужен реконфиг cmake в каждом билд-дереве** — сорцы собираются `file(GLOB_RECURSE)`, без реконфига будут undefined symbols на новых классах
- Подробно: `deploy/BUILD_RELEASE.md`

## Билды

- macOS: `deploy/build_macos_local.sh` (arm64/intel через `deploy/build-macos-local-*`), упаковка `deploy/package_macos_pkg.sh <build-dir>`
- Windows: по SSH на вин-машину (`Happy@192.168.0.127`, ключ `~/.ssh/dopamine_win`), `deploy/build_windows_local.bat` → MSI в `deploy/build_64/Dopamine-*-win64.msi`. Туннель (`tunnel.dll`) пересобирать **не нужно** — только при смене AWG (`deploy/build_tunnel_dll_windows.bat`)
- Android: `deploy/build_android.sh` → APK. На macOS нужен GNU getopt (`brew install gnu-getopt`, `PATH=/opt/homebrew/opt/gnu-getopt/bin:$PATH`), иначе скрипт молча печатает usage и выходит. 64-bit: `-a arm64-v8a`, 32-bit (ТВ/старые Android): `-a armeabi-v7a`.
  Env: `QT_HOST_PATH=/Users/2pizza/c/6.10.1/6.10.1/macos`, `ANDROID_NDK_ROOT=$HOME/Library/Android/sdk/ndk/26.1.10909125`, `ANDROID_KEYSTORE_PATH=$HOME/frkn-release-key.jks`, alias/пароль ключа — локально (не в репе). Нужен JDK 17 (`JAVA_HOME=$(/usr/libexec/java_home -v 17)`): на Java 25 Gradle 8.10 падает. Без `CI=1`/`-m` готовый APK остаётся в `deploy/build/client/android-build/build/outputs/apk/release/FRKN-<abi>-release.apk`.
- iOS: юзер собирает через Xcode из `build-ios-upload/`

**Правила:**
- Мак-билды читают рабочее дерево — не править код во время сборки и **не параллелить** мак-билды
- На винде сирота `cmd.exe` лочит `build.log` — при странных ошибках билда проверить зависшие процессы
- Ошибка «go.zip doesn't look like tar archive» в `build_tunnel_dll_windows.bat` = битый кэш в `%TEMP%\amneziawg-windows`, снести каталог

## Git

- Рабочая ветка: `dev`. Пуш: `GIT_SSH_COMMAND="ssh -i ~/.ssh/id_rsa_gh_2pizza -p 443 -o IdentitiesOnly=yes" git push git@ssh.github.com:frkn-dev/dopamine.git dev`
- Коммитить и пушить только по явному запросу юзера
- Не коммитить: логи, `__pycache__/`, `*.backup*`, сгенерированные хедеры (`Dopamine.h`), артефакты билдов, `*.msi`
- В корне лежат платёжные и подписные артефакты (`AuthKey_*.p8`, `agw_private.pem`, `*.key`, сертификаты) — не коммитить, не печатать в чат, не копировать наружу

## Деплой релиза

Живой сайт — **frkn.app**, а не frkn.org: nginx отдаёт `/opt/frkn.app/dopamine/index.html` как `https://frkn.app/`, а `dopamine/en/index.html` — как `https://frkn.app/en/`. Точные page-URL (`/dopamine/`, `/dopamine/en/`) и `frkn.org/dopamine/*` редиректят туда; файлы по `/dopamine/*.pkg|apk|bin` отдаются из `/opt/frkn.app/dopamine/`.

1. Бинарники → `~/c/f/frkn.org/dopamine/` (в гите сайта pkg/apk/bin заигнорены, MSI коммитить не надо)
2. Бамп ссылок и бейджа версии в `dopamine/index.html` и `dopamine/en/index.html`
3. Коммит в `frkn.org` (ветка feature/*, правила мёржа — в `~/c/f/frkn.org/AGENTS.md`)
4. Деплой: `./deploy-app.sh` (весь tree) или точечно, чтобы не выкатить чужие правки:
   `RSYNC_RSH="ssh -i ~/.ssh/ed25519_frkn -o IdentitiesOnly=yes" rsync -av --chmod=F644 dopamine/index.html root@141.133.173.16:/opt/frkn.app/dopamine/`
   (+ `dopamine/en/index.html` → `/opt/frkn.app/dopamine/en/`, новые бинарники → `/opt/frkn.app/dopamine/`)
5. Проверка: `curl -s https://frkn.app/ | grep -o 'v4\.8\.14\.[0-9]*'` (и `/en/`), `curl -sI https://frkn.app/dopamine/<файл>` — 200 и Content-Length = локальному. `/opt/frkn.org` — легаси-копия, frkn.app её не отдаёт.

## Известные грабли (не «чинить» без задачи)

- Пустой IPv6-exclusion `/999999` ломал мак — гард на месте
- При деактивации на маке обязателен stop handshake timer, иначе сироты wireguard-go
- AWG 3.1-параметры (I1–I5, junk) портированы на все платформы — не потерять при обновлении 3rd
- UI-фризы при старте были связаны с синхронными вызовами API — сетевое в фоне
- API таймауты у юзеров: базовый таймаут поднят, запасной эндпойнт обсуждён

## Соседние репозитории

| Репо | Что там | Когда лезть |
|---|---|---|
| `../ffcore` | API/ноды, `/sub/<token>`, конфиги подключений | менять поведение подписки/импорта |
| `../frkn-services` | payment-gw, mrkting, dashboard + релизные скрипты | биллинг, триалы, релизы серверных бинов |
| `../frkn.org` | сайт и витрина, откуда качают клиент | бамп ссылок/версий, деплой бинарников |
| `../env` | конфиги серверов, премиум-инвентарь, кросс-репо индекс | живая инфра, задачи на несколько реп |

## Стиль

- Минимальные диффы, код в стиле окружения, без преждевременных абстракций
- После изменения поведения — обновить комментарии и эту документацию, если затронуто описанное здесь
