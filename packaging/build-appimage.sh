#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds a portable AppImage in ./dist/. Run as root in the oldest Ubuntu you
# want to support (the AppImage needs a glibc at least as new as the build's):
#
#   docker run --rm -v "$PWD:/src" -w /src ubuntu:22.04 packaging/build-appimage.sh

set -eu

export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends build-essential cmake pkg-config \
    qt6-base-dev libqt6opengl6-dev qt6-wayland libturbojpeg0-dev libgl-dev \
    ca-certificates wget file patchelf libfuse2 squashfs-tools

ARCH=$(uname -m)
# The tools are "continuous" GitHub releases that are replaced in place, so a
# download can briefly fail while a new build is uploaded. Retry for a while.
fetch() {
    wget -q --tries=6 --waitretry=10 --retry-connrefused \
        --retry-on-http-error=404,429,500,502,503,504 -O "$1" "$2"
}
TOOLS=/tmp/appimage-tools
mkdir -p "$TOOLS"
for tool in linuxdeploy linuxdeploy-plugin-qt; do
    if [ ! -x "$TOOLS/$tool-$ARCH.AppImage" ]; then
        fetch "$TOOLS/$tool-$ARCH.AppImage" \
            "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-$ARCH.AppImage"
        chmod +x "$TOOLS/$tool-$ARCH.AppImage"
    fi
done
if [ ! -f "$TOOLS/runtime-$ARCH" ]; then
    fetch "$TOOLS/runtime-$ARCH" \
        "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-$ARCH"
fi
export LDAI_RUNTIME_FILE="$TOOLS/runtime-$ARCH"
# Containers have no FUSE; the tools unpack themselves instead.
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$TOOLS:$PATH"

BUILD=build-appimage
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DCAMTUNE_BUILD_TESTS=OFF
cmake --build "$BUILD" -j"$(nproc)"
rm -rf "$BUILD/AppDir"
DESTDIR="$PWD/$BUILD/AppDir" cmake --install "$BUILD"
# The polkit policy only works when installed system-wide.
rm -rf "$BUILD/AppDir/usr/share/polkit-1"

VERSION=$(sed -n 's/^project(CamTune VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
export QMAKE=/usr/lib/qt6/bin/qmake
[ -x "$QMAKE" ] || QMAKE=$(command -v qmake6)
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export LDAI_OUTPUT="CamTune-${VERSION}-${ARCH}.AppImage"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

cd "$BUILD"
"linuxdeploy-$ARCH.AppImage" --appdir AppDir \
    --desktop-file AppDir/usr/share/applications/io.github.CamTune.desktop \
    --icon-file AppDir/usr/share/icons/hicolor/256x256/apps/io.github.CamTune.png \
    --plugin qt --output appimage
cd ..
mkdir -p dist
mv "$BUILD/$LDAI_OUTPUT" dist/
echo "Built dist/$LDAI_OUTPUT"
