# HUMANS.md

## Current
- **Repo:** dopamine
- **Branch:** `dev`
- **Task:** Windows split-tunnel — фикс собран и установлен на бокс; остался прогон на железе с подключением
- **Status:** причина найдена и закрыта в коде: tunnel-процесс не должен поднимать демон. MSI (4.8.14.70, собран из рабочего дерева) установлен, после перезагрузки сервис стартует чисто: `Initialised Sublayer` → `Connected to the Driver`, простой без зависаний. Прогон «подключиться + `curl.exe` мимо VPN» не сделан: после reboot бокс на экране логина (AutoAdminLogon не настроен), GUI не поднять
- **Updated:** 2026-10-23

## Done
- 2026-10-23 — **настоящая причина split-tunnel**: `service/server/main.cpp` после `tunneldaemon` проваливался в `LocalServer localServer;` → tunnel-процесс (он же `dopamine-service.exe tunneldaemon "<config>"`) поднимал **второй WindowsDaemon**: второй firewall-движок, второй слушатель `\\.\pipe\frkn` и второе открытие драйвера. По логу: `Started as console application` → `Tunnel daemon service is starting` → через 1.2 с `Daemon created` + `Server path: \\.\pipe\frkn`, повтор каждые 2 с. Второй `create()` видел уже инициализированный драйвер, делал `resetDriver()` и стирал правила, которые только что залил сервис → сплит-туннель «включался» и тут же терялся, а дальше два процесса дрались за устройство, которое открывается эксклюзивно. Фикс: `return daemon.run(tokens);` — tunnel-процесс больше не строит LocalServer
- 2026-10-23 — `windowssplittunnel.cpp`: `CreateFileW` на драйвер теперь с `FILE_SHARE_READ|FILE_SHARE_WRITE` в обоих местах (было `dwShareMode=0`), в лог добавлен `GetLastError()`. Нужно как страховка на окно перезапуска сервиса, когда прошлый процесс ещё держит хендл
- 2026-10-23 — собран и установлен MSI: `build_win.sh` (sync рабочего дерева → сборка под schtasks → MSI на мак) → `msiexec /qn` через SYSTEM-задачу; в установленном `dopamine-service.exe` есть маркер нового кода, драйвер открывается
- 2026-10-23 — грабли скриптов хоста: (1) `build_win.sh` не удалял прошлый MSI, поэтому «готово» приходило через 8 с и забирался старый инсталлятор — теперь удаляет до старта задачи и падает, если не смог; (2) `deploy/run_windows_admin_task.ps1` падал на первом запуске (`schtasks /delete` по несуществующей задаче = terminating error при `ErrorActionPreference=Stop`); (3) он же писал лог/маркер в `$env:TEMP`, а `Set-Content -Encoding ASCII` превращал кириллический `C:\Users\куся\...` в `????` → лог не писался, вызывающий висел до таймаута. Лог/маркер теперь рядом со скриптом (`C:\setup`)
- 2026-10-23 — диагностика на железе: зависший демон держит WFP, и тогда **любой** новый демон виснет в `FwpmSubLayerGetByKey0` (`windowsfirewall.cpp:121`), т.е. после `kill -f` демона нужен reboot бокса, иначе сервис не поднимается
- 2026-10-22 — вин-билд-хост настроен и собирает: VS 2022 Build Tools, Qt 6.10.1 msvc2022_64, IFW 4.7, WiX 4.0.5 (+ UI/Util extensions), .NET SDK 8, CMake/Ninja/PortableGit в `C:\tools`, репо + 6 сабмодулей, zlib-пребилды. Скрипты: `deploy/setup_windows_build.ps1` (без elevation: портативные тулзы, Python/.NET в профиль, WiX через `dotnet tool`, PATH в user-scope), `deploy/setup_windows_admin.ps1` (VS через `vs_BuildTools.exe`, OpenSSH, power plan, Defender), `deploy/run_windows_admin_task.ps1` (админская часть как SYSTEM через schtasks — ssh на винде не elevated).
- 2026-10-22 — сборка из **рабочего дерева без коммита**: `~/Desktop/dopamine-win-bootstrap/sync_src.sh` собирает `git diff --binary` + untracked-исходники, накатывает на клон винды (`git checkout -- .` → `git apply`), затем сверяет `git diff | git hash-object` с обеих сторон — при совпадении сборка идёт ровно из этих исходников. `build_win.sh` = sync → сборка под schtasks → MSI на мак.
- 2026-10-22 — `windowssplittunnel.cpp`: `IP_ADAPTER_ADDRESSES_LH` **не содержит** `Ipv4IfIndex` (есть только `IfIndex`/`Ipv6IfIndex`), из-за чего файл вообще не компилировался (C2039 в двух местах). IPv4-индекс теперь берётся из `GetIpAddrTable()` по IPv4-адресу адаптера (`ipv4IfIndexFor`), `byIndex` ищет сначала в IPv6-пространстве индексов, затем в IPv4.
- 2026-10-22 — `deploy/build_windows_local.bat`: QT_ROOT_DIR ищется в `C:\Qt` и в старом пути бокса, добавлена внятная ошибка `:noqt`. Строка `set BUILD_AR` **была корректной в HEAD** — «обрезанный BUILD_AR» оказался артефактом маскировки `NAME=value` в выводе; проверять такие строки по кодам символов, а не глазами.
- 2026-10-22 — Windows app-split-tunneling: (1) список исключений теперь уходит в драйвер ДО регистрации IP (пока драйвер в READY) — иначе включившийся драйвер блокирует уже открытые сокеты игры; (2) адрес физического адаптера берём из `GetAdaptersAddresses`, матч по `IfIndex`/`Ipv6IfIndex` и по IPv4-индексу из `GetIpAddrTable` (раньше смешивались IPv4-индекс из `GetBestRoute` и IPv6-индекс Qt → чужой/пустой адрес → у исключённого приложения нет сети, Amnezia #3306); (3) `convertPath` нормализует `\\`/file://-URL и не конвертируемые пути больше не попадают в блоб (раньше молча пустая строка → исключение не работало); (4) отказ от конфига, где internet-адаптер = тоннель, и fail-safe `stop()` вместо полу-включённого драйвера.
- 2026-10-21 — `frkn://sub/shared/<uuid>`: ImportController распознаёт ссылку до `frkn://sub/`, CoreController гонит её тем же импортом с `shared=true`, ApiConfigsController собирает auth_data `{share_token}` для `/v1/services` + `/v1/config` (кэш config_rev для шар-подписок отключён). Сборка macOS локально зелёная.
- 2026-10-21 — AGENTS.md переписан по единому скелету (что читать первым, сборка/тесты, git, деплой, грабли, соседние репы); не закоммичено.
- 2026-10-21 — фикс импорта `frkn://sub/shared/<uuid>`: `mid(19)` → `mid(18)` (префикс = 18 символов, терялся первый символ UUID, regex не матчил → импорт падал всегда). Проверено отдельным Qt-бинарём: mid(19) match=false, mid(18) match=true. PKG пересобран.
- 2026-10-21 — падения демона на маке: `activate()` рекурсивно звал себя, когда `deactivate()` выходил рано по `m_deactivating` (регресс 83f2882f, 2026-10-06) и не чистил `m_connections` — 3035 кадров, stack overflow, launchd поднимал демон (14 крэшей 12–21.10 по `/var/log/Dopamine by FRKN/dopamine-service.log`). Guard в `client/daemon/daemon.cpp`, собран подписанный+нотаризованный PKG `deploy/build-macos-local/pkg/Dopamine-local-4.8.14.70.pkg`; не установлен и не закоммичен.
- 2026-10-20 — проверено: XHTTP/TLS идёт через Go TLS; `fp` применяется только для REALITY (`client/core/serialization/vless.cpp`). Для XHTTP fingerprint не передаётся.
- 2026-10-20 — `dev` на релизе 4.8.14.70.

## Next
- [ ] ГЛАВНОЕ: прогон на железе после логина юзера на боксе (после reboot интерактивной сессии нет, AutoAdminLogon выключен). Подключиться, затем в логе ждём `Pushing new Ruleset` + `Split tunnel addresses` + `New Network Config Applied || new State: STATE_RUNNING`, а `curl.exe` (он в исключениях) должен отдавать ISP-адрес `79.139.146.253`, не exit-IP сервера `93.89.220.48`
- [ ] если после коннекта демон опять замолчит в логе (зависание внутри WFP) — смотреть, не делает ли второй демон `resetDriver()` поверх включённого драйвера; лечится только reboot бокса
- [ ] вторая половина (проверить на прогоне): если в логе появится `Split tunnel manager is not initialized` — `config.m_vpnDisabledApps` пустой. Сборка конфига: `vpnconnection.cpp:765-779` → `localsocketcontroller.cpp:257` → парсинг в `daemon.cpp:406`. Реестр бокса на 23.10: `Conf/appsRouteMode=2`, `Conf/appsSplitTunnelingEnabled=true`, `Conf/ExceptApps` = iexplore.exe + curl.exe — данные на месте
- [ ] только после успешного теста — коммитить правки `client/platforms/windows/daemon/` в `dev` (сейчас они в рабочем дереве и собраны без коммита, см. `deploy/BUILD_RELEASE.md` §4.1)
- [ ] закоммитить новые скрипты хоста: `deploy/setup_windows_build.ps1`, `deploy/setup_windows_admin.ps1`, `deploy/run_windows_admin_task.ps1`
- [ ] отдать юзеру инструкцию: исключения работают только для приложений, запущенных ПОСЛЕ включения туннеля (сокет, открытый до engage, драйвер не умеет перевесить) — Steam/игру перезапускать после Connect.
- [ ] установить `deploy/build-macos-local/pkg/Dopamine-local-4.8.14.70.pkg` и погонять переподключения: в логе не должно быть серий «Already connected» / «Deactivation in progress» длиннее пары строк.
- [ ] проверить импорт шар-подписки: api 0.6.62 + mrkting 0.2.42 выкачены на прод 22.10 00:31/00:44, блокер снят.
- [ ] при необходимости Chrome fingerprint на XHTTP: вынести `fp` из REALITY-ветки и добавить в XHTTP-ссылки. Для совместимости не требуется.

## Blockers / Notes
- **Блокер прогона:** бокс `winbuild` (192.168.1.18, ssh-алиас `winbuild`, ключ `~/Desktop/dopamine-win-bootstrap/dopamine_win_build`) после reboot на экране логина, AutoAdminLogon выключен — GUI-тест (запуск `Dopamine.exe`, клик «Подключиться») без логина юзера невозможен. SSH и админские задачи (SYSTEM через schtasks) работают
- Установленный MSI: `C:\build\dopamine\Dopamine_x64.msi` на боксе и копия `/Users/2pizza/Desktop/Dopamine_x64.msi` (собрано 23.10 16:44 из рабочего дерева)
- Дерево чистое относительно HEAD; мусор untracked (не трогать): `.tmp_*.py`, `IMG_8287.JPG`, `deploy/*.pkg|*.bin`, `deploy/vk-turn-test/`, `screenshots/store/*`, `client/3rd-prebuilt/openssl/lib/` (внутри сабмодуля).
- `dev` ahead 4 of `origin/dev` — push не сделан (нужен явный запрос).
- Секреты (`.p8`, `agw_private.pem`) не коммитить.

## Related
- `AGENTS.md`
