Подготовка (один раз)

• Xcode 26.1.1 (/Applications/Xcode-26.1.1.app), в нём залогинен Apple ID с доступом к команде 455SJ7P6J3 — подпись automatic, API-ключ не нужен, xcodebuild использует сессию Xcode.
• Перед каждой заливкой подними номер билда в корневом CMakeLists.txt: DOPAMINE_VERSION 4.8.14.36 (последняя цифра = build number, App Store Connect не примет повторный/меньший). Заодно
APP_ANDROID_VERSION_CODE если параллельно Android.

iOS

```bash
  cd /Users/2pizza/c/f/dopamine
  export DEVELOPER_DIR=/Applications/Xcode-26.1.1.app/Contents/Developer

  # 1. Конфигурация Xcode-проекта (нужна после смены ветки/CMake-изменений, иначе можно пропустить)
  /Users/2pizza/c/6.10.1/6.10.1/ios/bin/qt-cmake . -B build-ios-upload -GXcode \
    -DQT_HOST_PATH=/Users/2pizza/c/6.10.1/6.10.1/macos -DDEPLOY=ON

  # 2. Архив
  xcodebuild -project build-ios-upload/Dopamine.xcodeproj \
    -scheme Dopamine -configuration Release \
    -destination 'generic/platform=iOS' \
    -archivePath build-ios-upload/Dopamine.xcarchive \
    archive

  # 3. Экспорт сразу в App Store Connect
  xcodebuild -exportArchive \
    -archivePath build-ios-upload/Dopamine.xcarchive \
    -exportOptionsPlist build-ios-upload/exportOptionsUpload.plist \
    -exportPath build-ios-upload/upload
```

macOS (Network Extension вариант)

```bash
  export DEVELOPER_DIR=/Applications/Xcode-26.1.1.app/Contents/Developer

  # 1. Конфигурация (аналогично, при необходимости)
  /Users/2pizza/c/6.10.1/6.10.1/macos/bin/qt-cmake . -B build-macos-tf -GXcode \
    -DQT_HOST_PATH=/Users/2pizza/c/6.10.1/6.10.1/macos -DMACOS_NE=TRUE -DCMAKE_BUILD_TYPE=Release -DDEPLOY=ON

  # 2. Архив
  xcodebuild -project build-macos-tf/Dopamine.xcodeproj \
    -scheme Dopamine -configuration Release \
    -destination 'platform=macOS' \
    -archivePath build-macos-tf/Dopamine.xcarchive \
    archive

  # 3. Заливка
  xcodebuild -exportArchive \
    -archivePath build-macos-tf/Dopamine.xcarchive \
    -exportOptionsPlist build-macos-tf/exportOptionsUpload.plist \
    -exportPath build-macos-tf/upload
```
