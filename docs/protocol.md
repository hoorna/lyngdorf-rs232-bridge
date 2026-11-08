# Serial protocol (summary)

A short summary of the TDAi-2170 serial protocol, enough to test the connection and to
understand the trace logs. For the complete command list, including the input and voicing
numbers, see the
[Lyngdorf TDAi-2170 external control manual](https://lyngdorf.steinwaylyngdorf.com/downloads/lyngdorf-tdai-2170-external-control-manual/).

The bridge itself does not interpret the protocol; it passes all bytes through unchanged.

## Serial settings

115200 baud, 8 data bits, no parity, 1 stop bit, no flow control.

## Message format

- Every command and request starts with `!`.
- A **request** ends with `?`. The reply repeats the request name with the value in
  parentheses.
- Parameters are put in parentheses.
- Messages end with CR LF (`\r\n`). The amplifier also accepts a single CR, a single LF, or
  LF CR; its replies always end with CR LF.
- Commands are not case sensitive.
- Invalid commands are silently ignored; there is no error reply.
- After `!SUBSCRIBE`, the amplifier reports changes by itself (input, power, mute, voicing,
  RoomPerfect position). These status messages look like a reply, followed by an extra `!`.
  `!SUBSCRIBEVOL` does the same for volume changes.

## Examples

| Send | Meaning | Example reply |
|------|---------|---------------|
| `!DEVICE?` | device type | `!DEVICE(TDAI-2170)` |
| `!VER?` | software version | `!VER(1.23a)` |
| `!PWR?` | power state | `!PWR(ON)` |
| `!ON` / `!OFF` | power on / standby | |
| `!VOL?` | volume, in tenths of a dB | `!VOL(-350)` = −35.0 dB |
| `!VOL(-400)` | set volume to −40.0 dB | |
| `!VOLUP` / `!VOLDN` | volume one step (0.5 dB) up / down | |
| `!MUTEON` / `!MUTEOFF` | mute / unmute | |
| `!SRC?` | current input number | `!SRC(6)` |
| `!SUBSCRIBE` | report status changes automatically | `!SRC(6)!` after an input change |

## Sending a command by hand

On the machine that runs ser2net or the bridge:

```sh
python3 -c 'import socket;s=socket.create_connection(("127.0.0.1",4001));s.sendall(b"!VOL?\r\n");s.settimeout(2);print(s.recv(100))'
```
