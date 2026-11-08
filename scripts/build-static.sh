#!/bin/sh
# Build a fully static lyngdorf_bridge (musl + libusb), for use on any Linux
# system of the same CPU architecture, regardless of distribution or glibc version.
#
# Runs inside an Alpine Linux container, from the root of the repository:
#   docker run --rm -v "$PWD:/src" -w /src alpine:3.20 sh scripts/build-static.sh
#
# Output in dist/:
#   lyngdorf_bridge-linux-<arch>   the static binary
#   LICENSE-libusb.txt             libusb licence (LGPL-2.1), libusb is linked in
#   sha256sums.txt                 checksums
set -eu

LIBUSB_VERSION="${LIBUSB_VERSION:-1.0.27}"
OUT="${OUT:-dist}"

apk add --no-cache build-base linux-headers pkgconf curl tar bzip2 file

case "$(uname -m)" in
    aarch64) ARCH=arm64 ;;
    x86_64)  ARCH=amd64 ;;
    *)       ARCH="$(uname -m)" ;;
esac

# libusb as a static library. udev support is disabled: it would pull in
# libudev, and the bridge does not use the hotplug functions it provides.
WORK="$(mktemp -d)"
curl -fsSL "https://github.com/libusb/libusb/releases/download/v${LIBUSB_VERSION}/libusb-${LIBUSB_VERSION}.tar.bz2" \
    | tar -xj -C "$WORK"
(
    cd "$WORK/libusb-${LIBUSB_VERSION}"
    ./configure --prefix="$WORK/prefix" --enable-static --disable-shared --disable-udev
    make -j"$(nproc)"
    make install
)

# The bridge itself, linked statically. Generic -O2: no CPU-specific tuning,
# so the binary runs on every CPU of this architecture.
make clean
PKG_CONFIG_PATH="$WORK/prefix/lib/pkgconfig" \
    make CXXFLAGS="-O2" LDFLAGS="-static" PKG_CONFIG="pkg-config --static"
strip lyngdorf_bridge

mkdir -p "$OUT"
BIN="lyngdorf_bridge-linux-${ARCH}"
mv lyngdorf_bridge "$OUT/$BIN"
cp "$WORK/libusb-${LIBUSB_VERSION}/COPYING" "$OUT/LICENSE-libusb.txt"
(cd "$OUT" && sha256sum "$BIN" > sha256sums.txt)

# Checks: really static, and it starts (-h prints usage and exits with code 2)
file "$OUT/$BIN"
file "$OUT/$BIN" | grep -Eq 'statically linked|static-pie linked'
set +e
"$OUT/$BIN" -h
rc=$?
set -e
[ "$rc" -eq 2 ]
echo "OK: $OUT/$BIN"
