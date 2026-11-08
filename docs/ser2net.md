# Route A: ser2net (kernel has the cp210x driver)

Use this route when `lsusb -t` shows `Driver=cp210x` for the cable. The kernel then
creates a serial device such as `/dev/ttyUSB0`, and [ser2net](https://github.com/cminyard/ser2net)
makes it available on TCP port 4001.

## Install

```sh
sudo apt install ser2net
```

## Find a stable device name

`/dev/ttyUSB0` can change if you connect other USB serial devices. The name under
`/dev/serial/by-id/` stays the same:

```sh
ls -l /dev/serial/by-id/
```

For the SH-RJ12B this looks like:

```
usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_<serial>-if00-port0 -> ../../ttyUSB0
```

## Configure

The ser2net documentation names `/etc/ser2net/ser2net.yaml` as the configuration file, but
some Debian and Raspberry Pi OS releases use `/etc/ser2net.yaml`. Check which one your
system uses:

```sh
ls /etc/ser2net.yaml /etc/ser2net/ser2net.yaml 2>/dev/null
systemctl cat ser2net | grep ExecStart      # shows -c <file> if the service sets one
```

Replace the contents of that file with:

```yaml
%YAML 1.1
---
connection: &lyngdorf
  accepter: tcp,4001
  connector: serialdev,/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_<serial>-if00-port0,115200n81,local
  options:
    kickolduser: true
```

- Replace the device path with the one you found above, or use `/dev/ttyUSB0`.
- `115200n81` is the serial setting of the TDAi-2170: 115200 baud, 8 data bits, no parity, 1 stop bit.
- `local` ignores the modem control lines, which the amplifier does not use.
- `kickolduser: true` lets a new connection from the app take over when an old one was not
  closed properly, for example after the phone went to sleep.

## Start

Run it in the foreground first to check the configuration. `-d` keeps ser2net in the
foreground and prints its log to the terminal; use the file path you found above:

```sh
sudo systemctl stop ser2net          # the package may have started the service already
sudo ser2net -d -c /etc/ser2net/ser2net.yaml
```

Then test it with the `!DEVICE?` command from the [README](../README.md#3-test).

If that works, stop it with Ctrl+C and enable the service:

```sh
sudo systemctl enable --now ser2net
systemctl status ser2net
```

To remove the service again:

```sh
sudo systemctl disable --now ser2net
```

## Optional: log the serial traffic

To see exactly what the app and the amplifier send each other, add a trace option:

```yaml
  options:
    kickolduser: true
    trace-both: '/var/log/ser2net/lyngdorf-trace.log'
```

The directory must exist (`sudo mkdir -p /var/log/ser2net`). The trace log keeps growing,
so only use it while debugging and remove the option afterwards.
