Ready-made binary of the C++ bridge for **64-bit ARM Linux** (arm64 / aarch64), such as a
Raspberry Pi 3, 4 or 5 with a 64-bit operating system.

The binary is statically linked: it includes libusb and the C library, so it runs on any
arm64 Linux distribution without installing anything else. See
[docs/bridge.md](https://github.com/hoorna/lyngdorf-rs232-bridge/blob/main/docs/bridge.md#download-a-ready-made-binary)
for installation.

| File | |
|------|---|
| `lyngdorf_bridge-linux-arm64` | the bridge |
| `sha256sums.txt` | checksum, verify with `sha256sum -c sha256sums.txt` |
| `LICENSE-libusb.txt` | licence of libusb (LGPL-2.1), which is linked into the binary |

libusb is linked in unmodified. The complete source code of the bridge is in this
repository, so you can rebuild it against your own version of libusb with
`scripts/build-static.sh`.
