# Bridge on a Diretta Target

In a [Diretta](https://www.diretta.link) setup, the Diretta Host and the Diretta Target are
often connected by a dedicated, direct network link. The Host is also on the home network;
the Target is only reachable from the Host.

The Target usually sits next to the amplifier, so that is where the USB-RS232 cable goes.
But the Lyngdorf Remote app on your phone cannot reach the Target directly. The solution is
to let the Host forward TCP port 4001 to the Target:

```
app ──home network──▶ Diretta Host :4001 ──socat──▶ Diretta Target :4001 ──USB──▶ TDAi-2170
```

In the app you then enter the IP address of the **Host**, port 4001.

## 1. Run the bridge on the Target

Set up ser2net or `lyngdorf_bridge` on the Target as described in [ser2net.md](ser2net.md)
or [bridge.md](bridge.md), and test it there with the `!DEVICE?` command from the
[README](../README.md#3-test).

On a Target, use the C++ version of `lyngdorf_bridge` rather than the Python version: it does
not wake the CPU while idle. Diretta Targets often run a minimal audio kernel without the
`cp210x` driver, in which case ser2net is not an option anyway.

## 2. Find the IP address of the Target

On the Target, show the IPv4 addresses of all interfaces:

```sh
ip -4 addr show
```

Use the address of the interface that connects to the Host. In the examples below this is
`TARGET_IP`; replace it with your own address.

If the link between Host and Target uses link-local addresses (`169.254.x.x`), check that
the Target gets the same address after a reboot. If not, give it a fixed address.

## 3. Forward the port on the Host

Install socat on the Host:

```sh
sudo apt install socat
```

Try it by hand first:

```sh
socat -d -d TCP-LISTEN:4001,fork,reuseaddr TCP:TARGET_IP:4001
```

`-d -d` shows every incoming connection and every connection made to the Target. Run the
`!DEVICE?` test on the Host in a second shell; it should give the same reply as on the
Target. Then try the app.

socat only connects to the Target when a client connects to the Host. If the Target
reboots, the next connection simply works again.

## 4. Make it permanent

Copy the unit file, replace `TARGET_IP` and enable it:

```sh
sudo cp init/systemd/lyngdorf-forward.service /etc/systemd/system/
sudo sed -i 's/TARGET_IP/<address of your Target>/' /etc/systemd/system/lyngdorf-forward.service
sudo systemctl daemon-reload
sudo systemctl enable --now lyngdorf-forward
```

For an OpenRC-based Host (e.g. GentooPlayer) see [gentoo.md](gentoo.md).

## Why socat and not an SSH tunnel?

An SSH tunnel (`ssh -L 4001:localhost:4001 ...`) also works, but it does not restart by
itself after a reboot or when the connection to the Target drops, and encryption adds
nothing on a direct link inside your own setup. socat as a service is simpler and restarts
automatically.

## Alternative: connect the cable to the Host

If the Host is within reach of the amplifier, you can also plug the cable into the Host and
run the bridge there. Then no forwarding is needed, and the Target does nothing besides
playing audio.
