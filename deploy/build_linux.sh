#!/bin/bash
echo "Build script started ..."

set -o errexit -o nounset


# Hold on to current directory
PROJECT_DIR=$(pwd)
DEPLOY_DIR=$PROJECT_DIR/deploy

mkdir -p $DEPLOY_DIR/build
BUILD_DIR=$DEPLOY_DIR/build

APP_DIR=$DEPLOY_DIR/AppDir
mkdir -p $APP_DIR

TOOLS_DIR=$DEPLOY_DIR/Tools
mkdir -p $TOOLS_DIR

CQTDEPLOYER_DIR=$TOOLS_DIR/cqtdeployer
mkdir -p $CQTDEPLOYER_DIR

echo "Project dir: ${PROJECT_DIR}" 
echo "Build dir: ${BUILD_DIR}"

APP_NAME=FRKN
APP_FILENAME=$APP_NAME.app
APP_DOMAIN=org.frkn.package

DEPLOY_DATA_DIR=$PROJECT_DIR/deploy/data/linux
PREBUILT_DEPLOY_DATA_DIR=$PROJECT_DIR/client/3rd-prebuilt/deploy-prebuilt/linux/client/bin
INSTALLER_DATA_DIR=$PROJECT_DIR/deploy/installer/packages/$APP_DOMAIN/data

PRO_FILE_PATH=$PROJECT_DIR/$APP_NAME.pro
QMAKE_STASH_FILE=$PROJECT_DIR/.qmake_stash

# Search Qt
if [ -z "${QT_VERSION+x}" ]; then
  QT_VERSION=6.6.2
  if [ -f /opt/Qt/$QT_VERSION/gcc_64/bin/qmake ]; then
    QT_BIN_DIR=/opt/Qt/$QT_VERSION/gcc_64/bin
  elif [ -f $HOME/Qt/$QT_VERSION/gcc_64/bin/qmake ]; then
    QT_BIN_DIR=$HOME/Qt/$QT_VERSION/gcc_64/bin
  elif [ -f /usr/lib/qt6/bin/qmake ]; then
    QT_BIN_DIR=/usr/lib/qt6/bin
  elif [ -f /usr/lib/x86_64-linux-gnu/qt6/bin/qmake ]; then
    QT_BIN_DIR=/usr/lib/x86_64-linux-gnu/qt6/bin
  fi
fi

echo "Using Qt in $QT_BIN_DIR"


# Checking env
$QT_BIN_DIR/qt-cmake --version
gcc -v

# Build App
echo "Building App..."
cd $BUILD_DIR

$QT_BIN_DIR/qt-cmake -S $PROJECT_DIR
cmake --build . -j2 --config release

# Build and run tests here

#echo "............Deploy.................."

cp -r $DEPLOY_DATA_DIR/* $APP_DIR
cp -r $PREBUILT_DEPLOY_DATA_DIR $APP_DIR/client

if [ ! -f $CQTDEPLOYER_DIR/cqtdeployer.sh ]; then
  wget -O $TOOLS_DIR/CQtDeployer.zip https://github.com/QuasarApp/CQtDeployer/releases/download/v1.5.4.17/CQtDeployer_1.5.4.17_Linux_x86_64.zip
  unzip -o $TOOLS_DIR/CQtDeployer.zip -d $CQTDEPLOYER_DIR/
  chmod +x -R $CQTDEPLOYER_DIR
fi


$CQTDEPLOYER_DIR/cqtdeployer.sh -bin $BUILD_DIR/client/Dopamine -qmake $QT_BIN_DIR/qmake -qmlDir $PROJECT_DIR/client/ui/qml/ -targetDir $APP_DIR/client/
$CQTDEPLOYER_DIR/cqtdeployer.sh -bin $BUILD_DIR/service/server/dopamine-service -qmake $QT_BIN_DIR/qmake -targetDir $APP_DIR/service/

deploy_icu() {
  local binary=$1
  local target_dir=$2

  LD_LIBRARY_PATH="$QT_BIN_DIR/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$binary" |
    awk '$1 ~ /^libicu(data|i18n|uc)\.so\.[0-9]+$/ && $3 ~ /^\// { print $3 }' |
    while read -r library; do
      cp -fL "$library" "$target_dir/"
    done
}

deploy_icu $BUILD_DIR/client/Dopamine $APP_DIR/client/lib
deploy_icu $BUILD_DIR/service/server/dopamine-service $APP_DIR/service/lib

# cqtdeployer follows the dependencies of the binaries, but QML plugins are
# loaded at runtime: Qt5Compat/GraphicalEffects (ColorOverlay, used by the
# controls) needs libQt6ShaderTools, which nothing links directly — the app then
# dies on the first screen with
#   cannot load .../libqtgraphicaleffectsprivateplugin.so: libQt6ShaderTools.so.6
# Qt 6.5+ also needs libxcb-cursor for the xcb platform plugin, and it is not
# part of a stock desktop install. Copy whatever the deployed ELF files still
# miss out of the Qt install (and, for system libraries, from the build host).
deploy_missing_libs() {
  local round missing name src

  for round in 1 2 3 4; do
    missing=$(
      for libdir in "$APP_DIR/client/lib" "$APP_DIR/service/lib"; do
        find "$APP_DIR" -type f \( -name '*.so' -o -name '*.so.*' -o -name '*.plugin' \) -print0 2>/dev/null |
          while IFS= read -r -d '' f; do
            LD_LIBRARY_PATH="$libdir:$QT_BIN_DIR/../lib" ldd "$f" 2>/dev/null | awk '/not found/ {print $1}'
          done
      done | sort -u
    )
    [ -z "$missing" ] && break

    for name in $missing; do
      src=$(ls "$QT_BIN_DIR/../lib/$name"* 2>/dev/null | head -1)
      # Optional Qt plugins (gtk3 platform theme, SQL drivers, wayland) pull in
      # system libraries we do not ship: leave those alone, they are not loaded.
      [ -z "$src" ] && continue
      for dest in "$APP_DIR/client/lib" "$APP_DIR/service/lib"; do
        cp -fL "$src" "$dest/"
      done
      echo "bundled missing library: $name"
    done
  done

  # Not a Qt library, so it never lands in the loop above.
  local xcb_cursor
  xcb_cursor=$(ldconfig -p | awk '/libxcb-cursor\.so\.0 /{print $NF; exit}')
  if [ -n "$xcb_cursor" ]; then
    cp -fL "$xcb_cursor" "$APP_DIR/client/lib/"
    echo "bundled $(basename "$xcb_cursor")"
  fi
}

deploy_missing_libs

echo "=== dependency check ==="
LD_LIBRARY_PATH=$APP_DIR/client/lib ldd $APP_DIR/client/bin/Dopamine | grep "not found" || echo "client: all libraries resolved"
LD_LIBRARY_PATH=$APP_DIR/service/lib ldd $APP_DIR/service/bin/dopamine-service | grep "not found" || echo "service: all libraries resolved"

mkdir -p $INSTALLER_DATA_DIR
rm -f $INSTALLER_DATA_DIR/data.7z

7z a $INSTALLER_DATA_DIR/data.7z $APP_DIR/*

ldd $CQTDEPLOYER_DIR/bin/binarycreator

cp -r $PROJECT_DIR/deploy/installer $BUILD_DIR

$CQTDEPLOYER_DIR/binarycreator.sh --offline-only -v -c $BUILD_DIR/installer/config/linux.xml -p $BUILD_DIR/installer/packages -f $PROJECT_DIR/deploy/Dopamine_Linux_Installer.bin
