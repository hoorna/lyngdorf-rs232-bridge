# Troubleshooting

## The `!DEVICE?` test returns nothing or times out

- Check that ser2net or the bridge is running, and that nothing else is using port 4001:
  `ss -ltnp | grep 4001`.
- Check that the amplifier is connected and switched on or in standby.
- Check the cable: the wires in positions 4, 5 and 6 of the new plug, and the crimp.
  A wire that does not make proper contact gives exactly this symptom.
  See [cable.md](cable.md).

## The test returns `b''`

The connection was accepted and immediately closed. This usually happens on a Diretta Host:
socat accepted the connection but could not reach the bridge on the Target. Run the test
on the Target itself and check that the bridge is running there. See [diretta.md](diretta.md).

## `-bash: !DEVICE?\r\n": event not found`

Bash interprets `!` inside double quotes. Put the Python code between single quotes, as in
the examples, or switch off history expansion with `set +H`.

## Bridge: `CP210x 10c4:ea60 not found or not accessible`

- Check that the cable is detected: `lsusb | grep -i 10c4`.
- If it is listed, the bridge lacks permission: run it as root, or install the udev rule
  (see [bridge.md](bridge.md#running-without-root)).
- If your cable has a different ID, pass it with `-d vid:pid`.

## Bridge: `Claiming interface failed: LIBUSB_ERROR_BUSY`

Another program is using the cable: a second bridge, the Python and C++ version at the same
time, or ser2net. Stop the other program.

## Bridge: `USB receive error: NO_DEVICE`

The cable was unplugged or reset. When the bridge runs as a service, it is restarted
automatically after 5 seconds.

## The app cannot connect

- In the app, use the IP address of the machine the app should connect to (on a Diretta
  setup: the Host) and port 4001.
- Check that a firewall on that machine allows incoming TCP connections on port 4001.
- Run the `!DEVICE?` test with that IP address from another computer on the network,
  instead of `127.0.0.1`.
