# Gentoo and GentooPlayer

Everything in the other documents applies; this page lists only the differences.
Gentoo uses `emerge` instead of `apt` and, by default, OpenRC instead of systemd.

## Packages

| Purpose | Package |
|---------|---------|
| C++ bridge (libusb with headers) | `dev-libs/libusb` |
| Python bridge | `dev-python/pyusb` |
| ser2net | `net-misc/ser2net` |
| Port forwarding on a Diretta Host | `net-misc/socat` |

```sh
emerge -av dev-libs/libusb        # usually already installed
```

Gentoo installs headers together with the library, so no separate `-dev` package is needed.
gcc is part of every Gentoo system; the C++ bridge builds with `make`.

## GentooPlayer kernels

GentooPlayer offers several kernel variants. Some of them do not include the `cp210x` and
`usbserial` drivers; for example, `7.0.1-GentooPlayer-RT-ULTRA-CLTO` lacks them, while
`7.0.1-GentooPlayer-RT-ST-CLTO` has them. Check with `lsusb -t` as described in the
[README](../README.md#2-check-whether-your-kernel-has-a-driver-for-the-cable). Without
these drivers, use `lyngdorf_bridge` ([bridge.md](bridge.md)); it does not need them.

Building the C++ bridge with the same CPU tuning as the rest of GentooPlayer on a Raspberry Pi 4:

```sh
make CXXFLAGS="-O2 -mcpu=cortex-a72 -flto=auto"
```

## OpenRC services

### Bridge

```sh
cp init/openrc/lyngdorf-bridge /etc/init.d/
chmod +x /etc/init.d/lyngdorf-bridge
rc-update add lyngdorf-bridge default
rc-service lyngdorf-bridge start
```

Check:

```sh
rc-service lyngdorf-bridge status
tail /var/log/lyngdorf-bridge.log
```

The log should show `Endpoints: OUT=0x02 IN=0x82`, followed by `Bridge listening on 0.0.0.0:4001`.
`supervise-daemon` restarts the bridge after 5 seconds if it stops.

For the Python version, change `command` and `command_args` in `/etc/init.d/lyngdorf-bridge`
as described in the comment at the top of that file.

Remove:

```sh
rc-service lyngdorf-bridge stop
rc-update del lyngdorf-bridge default
```

### Port forwarding on a Diretta Host

```sh
cp init/openrc/lyngdorf-forward /etc/init.d/
sed -i 's/TARGET_IP/<address of your Target>/' /etc/init.d/lyngdorf-forward
chmod +x /etc/init.d/lyngdorf-forward
rc-update add lyngdorf-forward default
rc-service lyngdorf-forward start
```

Remove:

```sh
rc-service lyngdorf-forward stop
rc-update del lyngdorf-forward default
```

### ser2net

On Gentoo the ser2net configuration file is `/etc/ser2net/ser2net.yaml`. Use the
configuration from [ser2net.md](ser2net.md), then:

```sh
rc-update add ser2net default
rc-service ser2net start
```

## GentooPlayer RamSystem

With RamSystem enabled, changes to the root filesystem may be lost after a reboot.
Disable it first (`ramsystem_disable`), reboot, make the changes above, and enable it again
afterwards (`ramsystem_enable`).
