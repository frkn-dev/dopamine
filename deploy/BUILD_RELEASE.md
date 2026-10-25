# Локальный релиз Dopamine (все 4 платформы)

Всё собирается с мака; Windows — через ssh на вин-ноут. Сборки мака строят
рабочее дерево (не git ref) — **не правь код, пока идёт сборка**. Мак-билды
между собой не параллелить; винду можно параллельно с маком.

## 0. Бамп версии

В корневом `CMakeLists.txt`:

```cmake
set(DOPAMINE_VERSION 4.8.14.54)       # 4-компонентная: major.minor.patch.tweak
set(APP_ANDROID_VERSION_CODE 2131)    # строго растёт, не зависит от версии
```

**Как бампать правильно.** Версия 4-компонентная, и платформы берут из неё
разное (всё считается автоматически в `CMakeLists.txt`):

- **App Store / TestFlight / macOS**: маркетинговая версия = первые 3 цифры
  (`4.8.14`), build number (`CFBundleVersion`) = 4-я (`54`). Поэтому багфиксы
  крутим 4-й цифрой: `4.8.14.54 → 4.8.14.55 → ...` — в App Store Connect это
  одна версия 4.8.14 с новыми билдами, а не отдельный релиз на каждый чих.
  Меняешь 3-ю цифру (`4.8.15.1`) — это уже новая версия в сторе, и 4-ю снова
  начинай с 1 (счётчик build number обнуляется вместе с маркетинг-версией).
- **Android**: `versionName` = вся 4-компонентная версия; `versionCode`
  увеличивай на 1 при КАЖДОМ релизе независимо от маркетинг-версии.
- **Windows**: MSI умеет только 3 компонента и игнорирует 4-ю при сравнении
  версий, поэтому в MSI уходит `major.minor.tweak` (`4.8.14.54 → 4.8.54`) —
  апгрейды продолжают работать. В свойствах файла и в UI версия полная.
- **В UI приложения** (бейдж в настройках) показывается полная версия
  `4.8.14.54`.

```bash
git add CMakeLists.txt
git commit -m "Release 4.8.14.54 (Android versionCode 2131)"
GIT_SSH_COMMAND="ssh -i ~/.ssh/id_rsa_gh_2pizza -p 443 -o IdentitiesOnly=yes" \
  git push git@ssh.github.com:frkn-dev/dopamine.git dev
```

## 1. Android APK (на маке, ~15–25 мин)

```bash
cd ~/c/f/dopamine
export PATH=/opt/homebrew/opt/gnu-getopt/bin:$PATH
export JAVA_HOME=/opt/homebrew/opt/openjdk@17
export ANDROID_SDK_ROOT=$HOME/Library/Android/sdk
export ANDROID_NDK_ROOT=$HOME/Library/Android/sdk/ndk/26.1.10909125
export QT_HOST_PATH=/Users/2pizza/c/6.10.1/6.10.1/macos
export ANDROID_KEYSTORE_PATH=$HOME/frkn-release-key.jks
export ANDROID_KEYSTORE_KEY_ALIAS=frkn-key
export ANDROID_KEYSTORE_KEY_PASS=qwerty
bash deploy/build_android.sh -a arm64-v8a
```

Результат: `deploy/build/client/android-build/build/outputs/apk/release/FRKN-arm64-v8a-release.apk`

## 2. macOS PKG — Apple Silicon, затем Intel (на маке, ~20–30 мин каждый)

```bash
cd ~/c/f/dopamine
# arm64
QT_BIN_DIR=/Users/2pizza/c/6.10.1/6.10.1/macos/bin MACOS_ARCH=arm64 \
  bash deploy/build_macos_local.sh
bash deploy/package_macos_pkg.sh deploy/build-macos-local-arm64
# intel
QT_BIN_DIR=/Users/2pizza/c/6.10.1/6.10.1/macos/bin MACOS_ARCH=x86_64 \
  bash deploy/build_macos_local.sh
bash deploy/package_macos_pkg.sh deploy/build-macos-local-x86_64
```

PKG нотаризуются автоматически. Ошибка notary `Error 68` — транзиент Apple,
просто перезапусти `package_macos_pkg.sh`.

## 3. iOS → TestFlight (на маке, ~20–30 мин)

После бампа версии или удаления файлов из проекта **обязателен реконфигур**.
Если `xcode-select` указывает на старый Xcode (например 16.x), а SDK нужен от
Xcode 26 — задай `DEVELOPER_DIR`, иначе Swift-часть (WireGuardKit) не соберётся
с ошибкой «this SDK is not supported by the compiler»:

```bash
cd ~/c/f/dopamine
export DEVELOPER_DIR=/Applications/Xcode-26.1.1.app/Contents/Developer
cmake -S . -B build-ios-upload -GXcode \
  -DCMAKE_TOOLCHAIN_FILE=/Users/2pizza/c/6.10.1/6.10.1/6.10.1/ios/lib/cmake/Qt6/qt.toolchain.cmake \
  -DQT_HOST_PATH=/Users/2pizza/c/6.10.1/6.10.1/macos \
  -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_DEPLOYMENT_TARGET=17

# архив
xcodebuild -project build-ios-upload/Dopamine.xcodeproj -scheme Dopamine \
  -configuration Release -destination 'generic/platform=iOS' \
  -archivePath build-ios-upload/Dopamine.xcarchive archive

# экспорт + загрузка в TestFlight
xcodebuild -exportArchive -archivePath build-ios-upload/Dopamine.xcarchive \
  -exportOptionsPlist build-ios-upload/exportOptionsUpload.plist \
  -exportPath build-ios-upload/export \
  -authenticationKeyPath "$PWD/AuthKey_9Y92B3WCJF.p8" \
  -authenticationKeyID 9Y92B3WCJF \
  -authenticationKeyIssuerID d3135078-58fb-4834-8f6e-b729e970ba87
```

`-authenticationKeyPath` требует **абсолютный** путь (поэтому `$PWD/`).

После загрузки билд 10–30 минут в «обработке» у Apple, потом появится в
App Store Connect → TestFlight.

Установка на подключённый айфон из локального Debug-билда:

```bash
xcrun devicectl install app build-ios-upload/client/Debug-iphoneos/Dopamine.app
```

## 4. Windows MSI (ssh на вин-ноут, ~20–30 мин)

### 4.0 Первичная настройка вин-хоста (один раз на машине)

Свежая винда не соберёт клиент «из коробки». Настройка разбита на два скрипта,
потому что ssh-сессия на Windows **никогда** не бывает elevated, а UAC удалённо
не подтвердить:

| Скрипт | Что делает | От кого |
|---|---|---|
| `deploy\setup_windows_admin.ps1` | VS 2022 Build Tools (MSVC x64 + Windows SDK), OpenSSH-сервер (служба, firewall, `DefaultShell=cmd.exe`), high-performance power plan, исключения Defender | админ |
| `deploy\setup_windows_build.ps1` | CMake, Ninja, PortableGit, Python, .NET SDK, WiX 4 CLI, Qt 6.10.1 msvc2022_64 (`-m all`), Qt IFW 4.7, клон репо с сабмодулями, zlib-пребилд | обычный пользователь |

Админскую часть можно запустить двумя способами:

```powershell
# 1) локально, из PowerShell от администратора
cd <repo>\deploy; .\setup_windows_admin.ps1 -Root C:\build -QtRoot C:\Qt

# 2) удалённо, без UAC — как SYSTEM через планировщик (это делает мак по ssh)
ssh -i ~/.ssh/dopamine_win <user>@<ip> ^
  "powershell -NoProfile -ExecutionPolicy Bypass -File C:\build\dopamine\deploy\run_windows_admin_task.ps1"
```

`run_windows_admin_task.ps1` создаёт задачу `DopamineWinAdminSetup` (`/ru SYSTEM
/rl HIGHEST`), ждёт её, печатает лог и возвращает её exit code. VS Build Tools
ставится официальным бутстраппером `vs_BuildTools.exe`, а не через winget: winget
в SYSTEM-контексте (без профиля пользователя) ломается.

Основная часть — от пользователя (в том числе по ssh, elevation не нужна):

```powershell
cd <repo>\deploy
.\setup_windows_build.ps1 -Root C:\build -QtRoot C:\Qt -SkipVs -SkipAdmin `
    -CloneUrl git@github.com:frkn-dev/dopamine.git `
    -GitHubKeyPath C:\setup\dopamine_win_build
```

Что важно знать до запуска:

- Всё в `setup_windows_build.ps1` ставится в `C:\tools` (cmake/ninja/PortableGit)
  и в профиль пользователя (Python, .NET SDK, `wix` через `dotnet tool`), пути
  дописываются в **user** PATH — `build_windows_local.bat` найдёт их в
  следующих ssh-сессиях.
- `-GitHubKeyPath` — приватный ключ с доступом к `frkn-dev/*` (сабмодули
  `client/3rd-prebuilt` 1.4 ГБ и `amneziawg-apple` приватные). Скрипт кладёт его
  в `%USERPROFILE%\.ssh\id_github` и прописывает
  `Host github.com → ssh.github.com:443` в `~/.ssh/config` + `known_hosts`
  (порт 22 у GitHub часто режется).
- `-AuthorizedKey` — содержимое `~/.ssh/dopamine_win.pub` с мака. Для аккаунта из
  Administrators Windows OpenSSH читает
  `C:\ProgramData\ssh\administrators_authorized_keys`, не пользовательский файл.
- `QT_ROOT_DIR` пишется в машинные переменные (если есть elevation), иначе в
  user-scope; отдельный `set` перед сборкой не нужен.
- Первый прогон — 40–60 мин (VS Build Tools ~15 мин, Qt ~10, сабмодуль 1.4 ГБ).
  Повторные: `-SkipVs -SkipTools -SkipQt -SkipAdmin`.
- В конце печатается чек-лист (vcvars64, qt-cmake, windeployqt, binarycreator,
  wix, signtool, cmake, ninja, git, zlib, пребилды, сертификат) — всё должно быть
  `[ok]`. `signtool` берётся из Windows SDK, он есть только внутри vcvars64 —
  это нормально.
- Подписи пока нет: без сертификата «Privacy Technologies OU» в
  `Cert:\CurrentUser\My` `signtool` в `build_windows.bat` ругается, но сборка
  продолжается и MSI получается неподписанным.

Проверка с мака (IP печатает сам скрипт в блоке `Next`):

```bash
ssh -i ~/.ssh/dopamine_win Happy@<ip> "cd C:\\build\\dopamine && git status --short && call deploy\\build_windows_local.bat > build.log 2>&1"
```

Если ключ доступа к репо новый, добавь его в GitHub (Settings → SSH keys) и
убедись, что у аккаунта есть доступ к приватным репам `frkn-dev/*`.


```bash
export WIN=Happy@<ip-вин-хоста>            # старый бокс был Happy@192.168.0.127
export WIN_REPO='C:\build\dopamine'        # = -Root в setup_windows_build.ps1 + \dopamine

ssh -i ~/.ssh/dopamine_win $WIN \
  "cd $WIN_REPO && git checkout -- client\\translations 2>nul & git pull origin dev && call deploy\\build_windows_local.bat > build_481454.log 2>&1"
```

Имя лога меняй под версию. Если сразу прилетает «Процесс не может получить
доступ к файлу, так как этот файл занят другим процессом» — лог держит сирота
cmd.exe от оборванной ssh-сессии (та, что висит на «Завершить выполнение
пакетного файла Y/N?»). Лечение:

```bash
ssh -i ~/.ssh/dopamine_win Happy@192.168.0.127 "tasklist | findstr /I \"cmd.exe\""
# прибить лишние (сессия Services, не твоя текущая):
# taskkill /PID <pid> /F
```

Результат: `C:\build\dopamine\deploy\build_64\Dopamine-*-win64.msi`.
Забрать на мак:

```bash
scp -i ~/.ssh/dopamine_win "$WIN:build_64/Dopamine-*-win64.msi" .
```

(если `-Root` другой — путь `deploy\build_64` относительно него)

Если туннельная либа менялась (раз в релиз не нужно): сначала на винде
`deploy\build_tunnel_dll_windows.bat` (качает Go + llvm-mingw, долго в первый
раз). `go.zip doesn't look like tar archive` — удали каталог
`%TEMP%\amneziawg-windows` и перезапусти.

### 4.1 Сборка из рабочего дерева, без коммита

Вин-клон берётся из GitHub, поэтому по умолчанию в MSI попадает только
закоммиченный код. Чтобы проверить правку, которую ещё рано коммитить, есть
`~/Desktop/dopamine-win-bootstrap/sync_src.sh`:

```bash
# 1. патч из рабочего дерева + untracked-исходники (артефакты отфильтрованы)
cd /Users/2pizza/c/f/dopamine
git diff --binary --no-color > /tmp/tracked.patch
git ls-files --others --exclude-standard | grep -Ev '<артефакты>' > /tmp/untracked.txt
tar -cf /tmp/untracked.tar -T /tmp/untracked.txt

# 2. сбросить клон на винде к HEAD и накатить патч
scp -i ~/.ssh/dopamine_win /tmp/tracked.patch /tmp/untracked.tar <user>@<ip>:C:/build/dopamine/.sync/
ssh -i ~/.ssh/dopamine_win <user>@<ip> \
  "cd /d C:\build\dopamine && git checkout -- . && git apply --whitespace=nowarn .sync\tracked.patch && tar -xf .sync\untracked.tar"

# 3. ПРОВЕРИТЬ, что исходники совпадают (иначе сборка бессмысленна)
git diff --binary --no-color | git hash-object --stdin          # на маке
ssh -i ~/.ssh/dopamine_win <user>@<ip> \
  "cd /d C:\build\dopamine && git diff --binary --no-color | git hash-object --stdin"
```

Хеши обязаны совпасть — это единственное доказательство, что MSI собран из тех
же исходников, а не «примерно из тех». Весь цикл одной командой:

```bash
~/Desktop/dopamine-win-bootstrap/build_win.sh            # sync + сборка + MSI на мак
~/Desktop/dopamine-win-bootstrap/build_win.sh --no-sync  # собрать то, что уже на боксе
```

Почему не «просто scp изменённых файлов»: новый файл, переименование или
удаление в такой схеме теряются молча, и проверить потом, что именно
скомпилировалось, нечем.

`C:\setup\run_build.bat` на боксе выставляет окружение этого хоста —
`QT_ROOT_DIR=C:\Qt`, `DOTNET_ROOT`, ASCII-`HOME=C:\home` (профиль пользователя
кириллический, на нём ломаются ssh и git), PATH с `C:\tools` — и запускает
`deploy\build_windows_local.bat`, дописывая лог в `C:\setup\build_msi.log`.
Запускать через `schtasks`, иначе сборка умрёт вместе с ssh-сессией.

## 5. Раскладка на сайт

Скопировать артефакты в `~/c/f/frkn.org/dopamine/` с именами
`frkn-dopamine-<версия>.apk`, `Dopamine-<версия>-win64.msi`,
`Dopamine-arm64-<версия>.pkg`, `Dopamine-intel-<версия>.pkg`, поправить версию
и дату в `~/c/f/frkn.org/dopamine/index.html` и `dopamine/en/index.html`.

Там же обновить ченжлог под бейджем версии (`details.version-notes`). Бейдж
на frkn.app открывает список. Каждая версия — свой `notes-block` с номером в
`notes-ver`, сверху свежая. Общие пункты — «Все платформы» / «All platforms».
Если правка только у одной платформы, внутри того же блока добавить
`notes-plat` (Android, iOS, Windows, macOS) и не писать её в общем списке.
Платформу без своих пунктов не добавлять. Старые версии в списке оставить,
самую давнюю убрать, когда блоков станет больше четырёх. Текст — что изменилось
для пользователя, на русском в `index.html` и по-английски в `en/index.html`.

```html
<div class="notes-block">
  <div class="notes-ver">4.8.14.61</div>
  <ul>
    <li>Убрали создание пробного аккаунта.</li>
  </ul>
</div>
```

Деплой сайта — отдельно, только после локальной проверки сборок. На сервер
уходят обе страницы: `dopamine/index.html` и `dopamine/en/index.html`.

## Типовые грабли

- **iOS**: `Dopamine-Swift.h file not found` после правок в common/logger —
  реконфигур build-ios-upload (см. п.3).
- **Мак/iOS**: сборки читают рабочее дерево — не трогай `git checkout`/правки
  во время сборки.
- **PowerShell через ssh** съедает `$_` — сложные скрипты клади .ps1 файлом
  через scp и запускай.
- **client/3rd-prebuilt** помечен modified (tunnel.dll, zlib) — это норма,
  не коммитить. Мусор (логи, Dopamine.h, __pycache__, *.backup*) — тоже.
