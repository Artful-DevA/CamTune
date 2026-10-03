#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds a .deb or .rpm for the distribution this runs on (normally a clean
# container of that distribution, as root). The package lands in ./dist/ with
# the distribution in its file name, e.g. camtune_0.1.0_ubuntu-24.04_amd64.deb
#
#   docker run --rm -v "$PWD:/src" -w /src ubuntu:24.04 packaging/build-package.sh
#   docker run --rm -v "$PWD:/src" -w /src fedora:42    packaging/build-package.sh

set -eu

. /etc/os-release
TAG="${ID}-${VERSION_ID:-rolling}"
BUILD="build-pkg-${TAG}"

if command -v apt-get >/dev/null 2>&1; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -q
    apt-get install -y -q --no-install-recommends build-essential cmake pkg-config \
        qt6-base-dev libqt6opengl6-dev libturbojpeg0-dev libgl-dev dpkg-dev file
    GEN=DEB
elif command -v dnf >/dev/null 2>&1; then
    dnf install -y -q gcc-c++ cmake pkgconf qt6-qtbase-devel turbojpeg-devel \
        mesa-libGL-devel rpm-build
    GEN=RPM
elif command -v zypper >/dev/null 2>&1; then
    zypper --non-interactive install gcc-c++ cmake pkgconf qt6-base-devel qt6-opengl-devel \
        libturbojpeg0 libjpeg-turbo-devel Mesa-libGL-devel rpm-build
    GEN=RPM
else
    echo "Unsupported distribution: $ID" >&2
    exit 1
fi

cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DCAMTUNE_BUILD_TESTS=ON
cmake --build "$BUILD" -j"$(nproc)"
ctest --test-dir "$BUILD" --output-on-failure -E soak
(cd "$BUILD" && cpack -G "$GEN")

mkdir -p dist
VERSION=$(sed -n 's/^project(CamTune VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
for f in "$BUILD"/camtune*."$(echo "$GEN" | tr 'A-Z' 'a-z')"; do
    case "$GEN" in
        DEB) arch=$(dpkg --print-architecture); out="camtune_${VERSION}_${TAG}_${arch}.deb" ;;
        RPM) arch=$(uname -m);                  out="camtune-${VERSION}-${TAG}.${arch}.rpm" ;;
    esac
    cp "$f" "dist/$out"
    echo "Built dist/$out"
done
