# Route B: lyngdorf_bridge (no kernel driver needed)

`lyngdorf_bridge` talks to the CP210x chip in the cable directly through libusb. It does not
need the `cp210x` or `usbserial` kernel drivers, which are missing from some stripped-down
audio kernels. It listens on TCP port 4001 and passes every byte unchanged between the app
and the amplifier.

There are two versions with the same behaviour:

| | C++ (`src/`) | Python (`python/`) |
|---|---|---|
| Needs | compiler, libusb headers | Python 3, pyusb |
| CPU when idle | none: sleeps until something happens | wakes up about 5 times per second |
| Recommended for | dedicated audio machines (e.g. a Diretta Target) | quick tests, or if you prefer not to compile |

Run only one of them at a time, and not together with ser2net: only one program can use
the cable.

## Download a ready-made binary

For 64-bit ARM Linux (arm64), such as a Raspberry Pi 3, 4 or 5 with a 64-bit operating
system, a ready-made C++ binary is available on the
[releases page](https://github.com/hoorna/lyngdorf-rs232-bridge/releases). Check that your
system is 64-bit ARM: `uname -m` must print `aarch64`.

The binary is statically linked: libusb and the C library are included, so you do not need
to install a compiler or libusb.

```sh
cd /tmp
curl -fLO https://github.com/hoorna/lyngdorf-rs232-bridge/releases/latest/download/lyngdorf_bridge-linux-arm64
curl -fLO https://github.com/hoorna/lyngdorf-rs232-bridge/releases/latest/download/sha256sums.txt
sha256sum -c sha256sums.txt
sudo install -m 0755 lyngdorf_bridge-linux-arm64 /usr/local/bin/lyngdorf_bridge
```

`sha256sum -c` must report `OK`. Then continue with [Run](#run) below.

The release binaries are built automatically by GitHub Actions
(`.github/workflows/release.yml`) with `scripts/build-static.sh`.

## C++ version

### Build and install

```sh
sudo apt install build-essential pkg-config libusb-1.0-0-dev git
git clone https://github.com/hoorna/lyngdorf-rs232-bridge.git
cd lyngdorf-rs232-bridge
make
sudo make install          # installs /usr/local/bin/lyngdorf_bridge
```

Fedora: `sudo dnf install gcc-c++ make pkgconf-pkg-config libusb1-devel git`.

### Run

```sh
sudo lyngdorf_bridge
```

Expected output:

```
2026-11-08 12:00:00 Endpoints: OUT=0x02 IN=0x82
2026-11-08 12:00:00 Bridge listening on 0.0.0.0:4001 -> CP210x @ 115200 8N1
```

Then test it with the `!DEVICE?` command from the [README](../README.md#3-test).
Stop the bridge with Ctrl+C.

| Option | Default | Meaning |
|--------|---------|---------|
| `-p` | `4001` | TCP port |
| `-l` | `0.0.0.0` | listen address (all interfaces) |
| `-b` | `115200` | baud rate |
| `-d` | `10c4:ea60` | USB vendor:product ID of the cable |

The defaults are correct for the SH-RJ12B and the TDAi-2170.

### Run as a service

```sh
sudo cp init/systemd/lyngdorf-bridge.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now lyngdorf-bridge
journalctl -u lyngdorf-bridge -f
```

systemd restarts the bridge after 5 seconds if it stops, for example after the cable was
unplugged.

To remove the service:

```sh
sudo systemctl disable --now lyngdorf-bridge
```

### Check that it really is idle

```sh
P=$(pidof lyngdorf_bridge)
grep ctxt /proc/$P/status; sleep 30; grep ctxt /proc/$P/status
```

While the app is not sending anything, `voluntary_ctxt_switches` must stay the same.

### How it works

- One asynchronous USB read without a timeout is always pending. The USB host controller
  polls the cable in hardware; the CPU only gets an interrupt when the amplifier sends data.
- The program has a single thread that sleeps in `epoll_wait()` without a timeout. It wakes
  up only for a new or active client, incoming USB data, or a stop signal.
- It reads the USB endpoints from the cable's descriptor, so it works with both the CP2102
  and the CP2102N chip.
- It shuts down cleanly on Ctrl+C or a service stop, and exits with code 1 on a USB error so
  that the service manager restarts it.

## Python version

### Install

```sh
sudo apt install python3-usb
sudo install -m 0755 python/lyngdorf_bridge.py /usr/local/bin/
```

Fedora: `sudo dnf install python3-pyusb`.

### Run

```sh
sudo python3 /usr/local/bin/lyngdorf_bridge.py
```

Expected output:

```
Endpoints: OUT=0x02 IN=0x82
Bridge listening on TCP port 4001 -> CP210x @ 115200 8N1
```

The port, baud rate and USB IDs are set at the top of the script.

### Run as a service

Use the same unit file as for the C++ version, but replace the `ExecStart` line in
`/etc/systemd/system/lyngdorf-bridge.service` with:

```ini
ExecStart=/usr/bin/python3 -u /usr/local/bin/lyngdorf_bridge.py
```

Then run `sudo systemctl daemon-reload` and `sudo systemctl enable --now lyngdorf-bridge`.

## Running without root

Accessing a USB device through libusb normally requires root. To run the bridge as a
normal user, install the udev rule and add the user to the `plugdev` group:

```sh
sudo cp udev/99-lyngdorf-bridge.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
sudo usermod -aG plugdev "$USER"           # log out and in again afterwards
```

Check with `groups` whether the user is already a member of `plugdev`.
