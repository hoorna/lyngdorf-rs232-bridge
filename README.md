# lyngdorf-rs232-bridge

Control a **Lyngdorf TDAi-2170** with the **Lyngdorf Remote** app through its RS232 port,
using a **DSD Tech SH-RJ12B** USB-to-RJ12 RS232 cable and a **Raspberry Pi** (or any Linux machine).

This replaces a network serial device server such as a Moxa NPort. The Pi exposes the
amplifier's serial port on TCP port 4001, and the app connects to it just as it would to a Moxa.

```
Lyngdorf Remote app ──Wi-Fi/LAN──▶ Linux machine :4001 ──USB──▶ SH-RJ12B cable ──RS232──▶ TDAi-2170
```

> Not affiliated with or endorsed by Lyngdorf Audio or DSD Tech.
> Tested with a TDAi-2170 and an SH-RJ12B cable (CP2102N chip) on a Raspberry Pi 4.
> Other Lyngdorf models may use the same protocol, but this has not been tested.

## Contents

| | |
|---|---|
| [docs/cable.md](docs/cable.md) | **Start here:** modifying the cable (the stock plug does not fit the TDAi-2170 pinout) |
| [docs/ser2net.md](docs/ser2net.md) | Route A: kernel has the `cp210x` driver → use ser2net |
| [docs/bridge.md](docs/bridge.md) | Route B: no `cp210x` driver → use `lyngdorf_bridge` (C++ or Python) |
| [docs/diretta.md](docs/diretta.md) | Bridge on a Diretta Target: port forwarding on the Diretta Host |
| [docs/gentooplayer.md](docs/gentooplayer.md) | Installation on GentooPlayer, with the differences for Gentoo |
| [docs/protocol.md](docs/protocol.md) | Short summary of the serial protocol |
| [docs/troubleshooting.md](docs/troubleshooting.md) | When something does not work |

Commands in this repository are for **Debian and Raspberry Pi OS**. For GentooPlayer and
Gentoo, see [docs/gentooplayer.md](docs/gentooplayer.md).

## Quick start

### 1. Modify the cable

The SH-RJ12B uses different pins than the TDAi-2170 expects. You need to crimp a new
RJ12 plug onto the cable: see [docs/cable.md](docs/cable.md).

### 2. Check whether your kernel has a driver for the cable

Plug the cable into a USB port and run:

```sh
lsusb | grep -i 10c4           # Silicon Labs CP210x, normally ID 10c4:ea60
lsusb -t                       # shows the driver per USB interface
```

- `Driver=cp210x` → the kernel driver is present, and the cable appears as `/dev/ttyUSB0`.
  Use **Route A**: [ser2net](docs/ser2net.md). This is the case on Raspberry Pi OS and most
  standard distributions.
- `Driver=[none]` → no kernel driver, for example on a stripped-down audio kernel.
  Use **Route B**: [lyngdorf_bridge](docs/bridge.md). It talks to the chip directly through
  libusb and needs no kernel driver.

Route B also works when the driver is present: `lyngdorf_bridge` detaches it automatically.

### 3. Test

On the machine that runs ser2net or the bridge:

```sh
python3 -c 'import socket;s=socket.create_connection(("127.0.0.1",4001));s.sendall(b"!DEVICE?\r\n");s.settimeout(2);print(s.recv(100))'
```

Expected reply:

```
b'!DEVICE(TDAI-2170)\r\n'
```

Keep the single quotes around the Python code; otherwise bash interprets the `!` in `!DEVICE?`.

### 4. Add the amplifier to the Lyngdorf Remote app

In the app, go to **Devices**, tap **+** and enter:

- **Name:** for example `TDAI-2170`
- **Host:** the IP address of the Linux machine
- **Port:** `4001`

### Diretta Host/Target setups

If the cable is connected to a Diretta Target, the app cannot reach the Target directly.
See [docs/diretta.md](docs/diretta.md) for the port forwarding on the Diretta Host.

## Repository layout

```
src/lyngdorf_bridge.cpp      event-driven C++ bridge (libusb)
python/lyngdorf_bridge.py    the same bridge in Python (pyusb), no compiler needed
Makefile                     builds the C++ bridge
init/systemd/                systemd units (bridge, port forward)
init/openrc/                 OpenRC services (bridge, port forward)
udev/                        udev rule to run the bridge without root
docs/                        documentation and diagrams
```

## License

[MIT](LICENSE)
