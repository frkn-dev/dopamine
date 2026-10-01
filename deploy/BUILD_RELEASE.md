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

```bash
ssh -i ~/.ssh/dopamine_win Happy@192.168.0.127 \
  "cd C:\Users\Happy\Desktop\CODE\dopamine && git checkout -- client\translations 2>nul & git pull origin dev && call deploy\build_windows_local.bat > build_481454.log 2>&1"
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

Результат: `C:\Users\Happy\Desktop\CODE\dopamine\deploy\build_64\Dopamine-*-win64.msi`.
Забрать на мак:

```bash
scp -i ~/.ssh/dopamine_win \
  "Happy@192.168.0.127:Desktop/CODE/dopamine/deploy/build_64/Dopamine-*-win64.msi" .
```

Если туннельная либа менялась (раз в релиз не нужно): сначала на винде
`deploy\build_tunnel_dll_windows.bat` (качает Go + llvm-mingw, долго в первый
раз). `go.zip doesn't look like tar archive` — удали каталог
`%TEMP%\amneziawg-windows` и перезапусти.

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
