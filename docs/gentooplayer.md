# GentooPlayer / Gentoo

The installation follows the other documents, which are written for Debian. This page only
lists what is different on **GentooPlayer** (Raspberry Pi 4). Additional differences for a
regular Gentoo installation are listed at the end.

## General differences

- **Root:** on GentooPlayer you work as root. Leave out `sudo`.
- **Packages:** install with `emerge` instead of `apt`:

  | Purpose | Debian | GentooPlayer |
  |---------|--------|--------------|
  | ser2net | `ser2net` | `net-misc/ser2net` |
  | socat (Diretta Host) | `socat` | `net-misc/socat` |
  | C++ bridge | `build-essential pkg-config libusb-1.0-0-dev` | already installed |
  | Python bridge | `python3-usb` | `dev-python/pyusb` |

- **Services:** GentooPlayer uses OpenRC instead of systemd. The service files are in
  `init/openrc/`.

  | systemd | OpenRC |
  |---------|--------|
  | `cp init/systemd/<name>.service /etc/systemd/system/` | `cp init/openrc/<name> /etc/init.d/ && chmod +x /etc/init.d/<name>` |
  | `systemctl enable --now <name>` | `rc-update add <name> default && rc-service <name> start` |
  | `systemctl disable --now <name>` | `rc-service <name> stop && rc-update del <name> default` |
  | `systemctl status <name>` | `rc-service <name> status` |
  | `journalctl -u lyngdorf-bridge` | `tail /var/log/lyngdorf-bridge.log` |

- **RamSystem:** with RamSystem enabled, changes to the root filesystem may be lost after a
  reboot. Disable it first (`ramsystem_disable`), reboot, do the installation, and enable it
  again afterwards (`ramsystem_enable`).

## Which route

GentooPlayer offers several kernel variants:

- **ST kernels** include the `cp210x` driver. Use **Route A**: [ser2net](ser2net.md).
- **Other kernels**, such as `7.0.1-GentooPlayer-RT-ULTRA-CLTO`, do not include the driver.
  Use **Route B**: the C++ version of [lyngdorf_bridge](bridge.md).

Check with `lsusb -t` as described in the
[README](../README.md#2-check-whether-your-kernel-has-a-driver-for-the-cable).

## Route A: ser2net

Differences from [ser2net.md](ser2net.md):

- The configuration file is `/etc/ser2net/ser2net.yaml`.
- Enable the service with `rc-update add ser2net default` and `rc-service ser2net start`.

## Route B: lyngdorf_bridge

Differences from [bridge.md](bridge.md):

- git is not needed. Download the code with wget:

  ```sh
  cd /root
  wget https://github.com/hoorna/lyngdorf-rs232-bridge/archive/refs/heads/main.tar.gz -O lyngdorf-rs232-bridge.tar.gz
  tar xzf lyngdorf-rs232-bridge.tar.gz
  cd lyngdorf-rs232-bridge-main
  ```

- Build with the same CPU tuning as the rest of GentooPlayer:

  ```sh
  make CXXFLAGS="-O2 -mcpu=cortex-a72 -flto=auto"
  make install
  ```

- Instead of building, you can also use the
  [ready-made binary](bridge.md#download-a-ready-made-binary). It is built for any arm64
  CPU instead of specifically for the Raspberry Pi 4; for this program that makes no
  noticeable difference. Leave out `sudo` in the commands.
- The service is `init/openrc/lyngdorf-bridge`. For the Python version, change `command` and
  `command_args` as described in the comment at the top of that file.

## Differences on a regular Gentoo installation

- **Kernel:** ser2net needs `CONFIG_USB_SERIAL_CP210X`. Without it, use `lyngdorf_bridge`.
- **Packages:** the C++ bridge needs `dev-libs/libusb`; install it if it is not present.
- **Compiler flags:** `-mcpu=cortex-a72` is only right for a Raspberry Pi 4. On other
  hardware, build with plain `make`, or use the CPU flags from your own `make.conf`.
- **systemd:** on a Gentoo system with systemd, use the unit files in `init/systemd/` and the
  systemd commands from the other documents.
