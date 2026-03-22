pip install aqtinstall
aqt install-qt mac desktop 6.6.2 clang_64 -m qtremoteobjects qt5compat qtshadertools qtmultimedia qtimageformats

Build the project:
```
git submodule update --init --recursive
mkdir build
cd build/
cmake .. -DQt6_DIR=~/Documents/6.6.2/macos/lib/cmake/Qt6 -DQt6QmlTools_DIR=~/Documents/6.6.2/macos/lib/cmake/Qt6QmlTools -G Ninja
ninja
```