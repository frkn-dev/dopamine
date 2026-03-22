Common solution here:
https://github.com/amnezia-vpn/amnezia-client/tree/dev?tab=readme-ov-file#how-to-build-an-ios-app-from-source-code-on-macos

Install Qt:
```bash
pip install aqtinstall
aqt install-qt mac ios 6.6.2 -m qtremoteobjects qt5compat qtshadertools qtmultimedia qtimageformats
```

Build the project:
```bash
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
~/Qt/6.6.2/ios/bin/qt-cmake . -B build-ios -GXcode -DQT_HOST_PATH=$QT_MACOS_ROOT_DIR -DQT_HOST_PATH_CMAKE_DIR=$QT_MACOS_ROOT_DIR
```