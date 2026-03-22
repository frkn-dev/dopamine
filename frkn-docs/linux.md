All packages that are needed to run qt installer, build and deploy the project:
```
sudo apt-get install build-essential libgl1-mesa-dev build-essential libgl1-mesa-dev libxkbcommon-x11-0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb1 libxcb-util1 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-render0 libxcb-shape0 libxcb-shm0 libxcb-xfixes0 libxcb-xinerama0 libxcb-randr0 libxcb-xkb1 libdbus-1-3 p7zip-full libxcb-cursor0 libxcb-cursor-dev gettext envsubst libglib2.0-0 libsecret-1-dev gcc cmake ninja-build python3 x11-apps wget unzip 7z -y
```

Run Qt Installer (link in [links.md](./links.md)):
```
sudo chmod +x qt-online-installer-linux-x64-4.8.1.run
./qt-online-installer-linux-x64-4.8.1.run
```

Build the project:
```
git submodule update --init --recursive
mkdir build
cd build/
cmake .. -DQt6_DIR=~/Qt/6.7.3/gcc_64/lib/cmake/Qt6 -DQt6QmlTools_DIR=~/Qt/6.7.3/gcc_64/lib/cmake/Qt6QmlTools -G Ninja
ninja
```

Deploy the project:
```
QT_BIN_DIR=~/Qt/6.7.3/gcc_64/bin bash ./deploy/build_linux.sh
```