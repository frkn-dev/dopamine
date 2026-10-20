# Локальные подсети в обход VPN (local subnet exclusions)

Настройка «Локальные подсети»: список IPv4/IPv6-подсетей, которые **всегда**
идут мимо туннеля — остаются доступными при включённом KillSwitch и работают
независимо от сайтового/пресетного сплит-туннеля.

Это не направление сплит-туннеля, а **маршрут вокруг туннеля**: записи
приоритетнее, чем «через VPN» из сайтового списка или пресета.

## Контракт и нормализация

Хранилище — `Conf/localSubnetExclusions` (`Settings::localSubnetExclusions()` /
`setLocalSubnetExclusions()`), строка-список.

Единая точка нормализации — `NetworkUtilities::normalizeSubnetExclusions()`
(`client/core/networkUtilities.cpp`), вызывается и на записи, и на чтении
(`Settings`), и в модели UI, и в демоне:

- `192.168.0.0/16`, `fd00::/8`, `10.0.0.1` — принимаются;
- «голый» адрес достраивается до `/32` (IPv4) или `/128` (IPv6);
- дубликаты (в т.ч. приведённые к каноничному виду) отбрасываются;
- `/0`, а также мусор/домены/строки с пробелами и некорректные префиксы —
  **отклоняются** (исключить «весь интернет» нельзя, это сломало бы VPN);
- DNS-домены сюда не входят — только адреса/подсети.

Пустой список = локальных исключений нет (поведение по умолчанию).

## Прокидывание в платформы

1. `VpnConnection` безусловно кладёт `config_key::localSubnetExclusions`
   (`localSubnetExclusions`) в `m_vpnConfiguration` — рядом с
   `splitTunnelIncludeSites` / `splitTunnelExcludeSites`.
2. Desktop (AWG через демон): `LocalSocketController` добавляет подсети в
   `excludedAddresses` **вне** условия `splitTunnelType == 2`; демон строит
   exclusion-маршруты через `IPAddress` (префикс сохраняется) —
   `client/daemon/daemon.cpp`.
3. macOS: демон (`WireguardUtilsMacos`) при включённом KillSwitch активирует
   отдельный PF-anchor `260.allowExcludedSubnets` (после `120.blockNets` и
   `250.blockIPv6`): исключённая подсеть проходит наружу, даже если перекрывается
   блокирующим диапазоном, и в том числе по IPv6.
   Файл anchor'а: `deploy/data/macos/pf/frkn.260.allowExcludedSubnets.conf`
   (включается из `deploy/data/macos/pf/frkn.conf`), таблица `excludedsubnets`
   заполняется из `FirewallParams::excludedSubnets`.
4. Linux: те же подсети попадают в allow-anchor `110.allowNets`
   (`LinuxFirewall`, теперь для **обеих** семей: IPv4 → `iptables`,
   IPv6 → `ip6tables`), поэтому исключения работают и в режиме `100.blockAll`,
   и когда перекрываются `120.blockNets`.
5. Windows: WFP-правило `allowTrafficTo` уже выдаёт разрешение на каждый
   `excludedAddress` (префикс сохраняется через `IPAddress`), маршруты
   обходятся `WindowsRouteMonitor::addExclusionRoute`. Попутно исправлено
   удаление маршрута, терявшее префикс (`delExclusionRoute(QHostAddress)` →
   `delExclusionRoute(IPAddress)`).
6. Android: `Protocol.kt::configLocalSubnetExclusions()` вызывается из
   `Wireguard`/`Awg`/`Xray` **безусловно** (в т.ч. когда сплит-туннель
   выключен). API 33+ — platform `excludeRoute()`; более старые версии —
   вычитание исключений из маршрутов туннеля (`IpRangeSet`). Android не
   позволяет одновременно include- и exclude-список, поэтому при
   include-режиме сайтов исключения вырезаются из include-списка, а при
   узких серверных AllowedIPs — из маршрутов туннеля (туннель не
   расширяется до `0.0.0.0/0`).
7. iOS/macOS NE: `WGConfig.localSubnetExclusions` → `peers[].excludeIPs` →
   `NEPacketTunnelNetworkSettings.excludedRoutes`
   (`PacketTunnelSettingsGenerator`). Для Xray-туннеля (IPv6 в нём не
   туннелируется) исключения добавляются в `excludedRoutes` после применения
   сайтового режима, чтобы он их не перезаписал.

Серверные `AllowedIPs`/`AllowedIps` не переписываются: исключения — это
локальные маршруты/правила, сам конфиг туннеля остаётся как есть.

## UI

- Страница `PageSettingsLocalSubnets` (`client/ui/qml/Pages2/PageSettingsLocalSubnets.qml`),
  выход — из «Сплит-туннеля по сайтам» («Локальные подсети всегда в обход VPN»).
- Модель `LocalSubnetExclusionsModel`, контроллер
  `LocalSubnetExclusionsController` (добавление/удаление/очистка, ошибки
  валидации — в нотификацию; вставка списка через пробел/запятую/точку с
  запятой добавляет несколько подсетей сразу).
- Локализации — только `en`/`ru`/`uk`.

## Когда применяется

Настройка попадает в конфиг следующего подключения (как и остальные параметры
сплит-туннеля): при активном подключении изменения вступят в силу после
переподключения.

## Проверка

- `Conf/localSubnetExclusions` — список нормализован (нет дублей, нет `/0`).
- Повторный `addSubnet` дубля → ошибка «уже в списке».
- Подключиться (AWG, desktop) с исключением домашней подсети (`192.168.0.0/16`):
  при включённом KillSwitch подсеть остаётся доступной (`ping`).
- macOS: `sudo pfctl -a frkn/260.allowExcludedSubnets -s rules` показывает
  таблицу `excludedsubnets`.
- Linux: `sudo iptables -S frkn.110.allowNets` / `ip6tables -S frkn.110.allowNets`.
- Windows: `route print` — маршрут на исключённую подсеть через физический
  шлюз с правильным префиксом.
- Android (API 33+): `dumpsys` / лог `Protocol` — `excludeRoute` на подсеть.
