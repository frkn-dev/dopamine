# Dopamine by FRKN — AGENTS.md

VPN-клиент (форк AmneziaVPN): Qt6/QML, C++20. Платформы: macOS (arm64/intel), iOS, Android, Windows.
Протоколы: AmneziaWG (основной, v3.1+ с I1–I5/junk-пакетами), Xray/VLESS. OpenVPN и SSH-серверы выпилены — не воскрешать без явного запроса.

## Навигация по репо

- `client/` — приложение (QML UI в `client/ui/`, платформенный код в `client/platforms/`, демон в `client/daemon/`)
- `service/` — системный сервис (Windows/macOS privileged helper)
- `ipc/` — IPC между приложением и сервисом (.rep, Qt RemoteObjects)
- `deploy/` — билд-скрипты всех платформ + `BUILD_RELEASE.md` (релизный процесс, схема версий)
- `client/3rd-prebuilt/` — пребилды (tunnel.dll и пр.) — в гите, не пересобирать без необходимости
- `client/translations/` — локализация, см. раздел ниже
- `frkn-docs/` — внутренняя документация по API и протоколам

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
- Android: `deploy/build_android.sh` → APK
- iOS: юзер собирает через Xcode из `build-ios-upload/`

**Правила:**
- Мак-билды читают рабочее дерево — не править код во время сборки и **не параллелить** мак-билды
- На винде сирота `cmd.exe` лочит `build.log` — при странных ошибках билда проверить зависшие процессы
- Ошибка «go.zip doesn't look like tar archive» в `build_tunnel_dll_windows.bat` = битый кэш в `%TEMP%\amneziawg-windows`, снести каталог

## Деплой релиза

1. Бинарники → `~/c/f/frkn.org/dopamine/` (в гите сайта pkg/apk/dmg заигнорены, MSI коммитить не надо)
2. Бамп ссылок и бейджа версии в `dopamine/index.html` (и `dopamine/en/index.html`)
3. Коммит в `frkn.org` (ветка feature/*, правила мёржа — в `~/c/f/frkn.org/AGENTS.md`)
4. **frkn.app** сейчас смотрит в `/opt/beta/frkn.org` (не prod):
   `RSYNC_RSH="ssh -i ~/.ssh/ed25519_frkn -o IdentitiesOnly=yes" ./deploy-beta.sh`
   — HTML + бинарники (второй проход с `chmod 644`, иначе nginx не отдаёт MSI с режимом 600)
5. Старый prod (`./deploy-site.sh` → `/opt/frkn.org`) — только если vhost frkn.org/dopamine ещё нужен
6. Проверка: `curl -sI https://frkn.app/dopamine/<файл>` — 200 и Content-Length = локальному размеру
   Лендинг: `https://frkn.app/` (= dopamine/index.html); путь `/dopamine/` для файлов, `/dopamine` HTML → `/`

## Git

- Рабочая ветка: `dev`. Пуш: `GIT_SSH_COMMAND="ssh -i ~/.ssh/id_rsa_gh_2pizza -p 443 -o IdentitiesOnly=yes" git push git@ssh.github.com:frkn-dev/dopamine.git dev`
- Коммитить и пушить только по явному запросу юзера
- Не коммитить: логи, `__pycache__/`, `*.backup*`, сгенерированные хедеры (`Dopamine.h`), артефакты билдов, `*.msi`

## Известные грабли (не «чинить» без задачи)

- Пустой IPv6-exclusion `/999999` ломал мак — гард на месте
- При деактивации на маке обязателен stop handshake timer, иначе сироты wireguard-go
- AWG 3.1-параметры (I1–I5, junk) портированы на все платформы — не потерять при обновлении 3rd
- UI-фризы при старте были связаны с синхронными вызовами API — сетевое в фоне
- API таймауты у юзеров: базовый таймаут поднят, запасной эндпойнт обсуждён
- VK TURN MVP (ветка `feature/vk-turn-android-mvp`): перед Android-сборкой `./deploy/fetch_vkturn_client.sh`; тестовый VPS = plain WG + free-turn-proxy server; см. `frkn-docs/vk-turn-mvp.md`

## Стиль

- Минимальные диффы, код в стиле окружения, без преждевременных абстракций
- После изменения поведения — обновить комментарии и эту документацию, если затронуто описанное здесь
