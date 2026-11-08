#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
TCP <-> CP210x (USB-RS232) bridge for the Lyngdorf TDAi-2170 - Python version.

Replaces a serial device server such as a Moxa NPort in "Reverse Telnet" mode:
the Lyngdorf Remote app connects to <ip>:4001 and every byte is passed
unchanged to and from the amplifier's RS232 port.
Talks to the CP210x chip directly through libusb (pyusb), so no kernel driver
(cp210x / usbserial) is needed.

Serial settings per the Lyngdorf external control documentation:
115200 baud, 8N1, no flow control.

Note: this version reads USB with a 200 ms timeout, so it wakes the CPU about
five times per second even when idle. The C++ version (src/) does not.
"""
import errno
import os
import signal
import socket
import struct
import sys
import threading

import usb.core
import usb.util

VID, PID = 0x10C4, 0xEA60      # Silicon Labs CP210x (check with lsusb)
BAUD = 115200
TCP_PORT = 4001
EP_OUT, EP_IN = 0x01, 0x81     # default; read from the descriptor in open_cp210x()
REQ = 0x41                     # host->device, vendor, interface (AN571)

USBTimeout = getattr(usb.core, "USBTimeoutError", ())

clients = set()
clients_lock = threading.Lock()
usb_write_lock = threading.Lock()
stop = threading.Event()


def open_cp210x():
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit("CP210x not found (check lsusb)")
    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except (NotImplementedError, usb.core.USBError):
        pass
    dev.set_configuration()
    usb.util.claim_interface(dev, 0)
    dev.ctrl_transfer(REQ, 0x00, 0x0001, 0)                     # IFC_ENABLE
    dev.ctrl_transfer(REQ, 0x1E, 0, 0, struct.pack("<I", BAUD))  # SET_BAUDRATE
    dev.ctrl_transfer(REQ, 0x03, 0x0800, 0)                     # SET_LINE_CTL 8N1
    dev.ctrl_transfer(REQ, 0x07, 0x0303, 0)                     # SET_MHS: DTR+RTS high

    # Bulk endpoints from the descriptor (CP2102: 0x01/0x81, CP2102N: 0x02/0x82)
    global EP_OUT, EP_IN
    intf = dev.get_active_configuration()[(0, 0)]
    for ep in intf.endpoints():
        if usb.util.endpoint_type(ep.bmAttributes) != usb.util.ENDPOINT_TYPE_BULK:
            continue
        if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN:
            EP_IN = ep.bEndpointAddress
        else:
            EP_OUT = ep.bEndpointAddress
    print(f"Endpoints: OUT=0x{EP_OUT:02x} IN=0x{EP_IN:02x}")
    return dev


def usb_reader(dev):
    """Forward everything the amplifier sends (replies and status messages) to all clients."""
    while not stop.is_set():
        try:
            data = bytes(dev.read(EP_IN, 64, timeout=200))
        except USBTimeout:
            continue
        except usb.core.USBError as e:
            if e.errno == errno.ETIMEDOUT:
                continue
            if stop.is_set():           # shutting down: not a real error
                return
            print(f"USB error: {e}", file=sys.stderr)
            # Exit so the service manager restarts us (e.g. cable unplugged)
            os._exit(1)
        if not data:
            continue
        with clients_lock:
            for c in list(clients):
                try:
                    c.sendall(data)
                except OSError:
                    clients.discard(c)


def handle_client(conn, addr, dev):
    print(f"Client connected: {addr[0]}:{addr[1]}")
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    with clients_lock:
        clients.add(conn)
    try:
        while True:
            data = conn.recv(256)
            if not data:
                break
            with usb_write_lock:
                dev.write(EP_OUT, data, timeout=1000)
    except (OSError, usb.core.USBError):
        pass
    finally:
        with clients_lock:
            clients.discard(conn)
        conn.close()
        print(f"Client disconnected: {addr[0]}:{addr[1]}")


def _on_sigterm(signum, frame):
    # A service stop or kill sends SIGTERM: shut down the same way as Ctrl+C
    raise KeyboardInterrupt


def main():
    signal.signal(signal.SIGTERM, _on_sigterm)
    dev = open_cp210x()
    reader = threading.Thread(target=usb_reader, args=(dev,), daemon=True)
    reader.start()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("", TCP_PORT))
    srv.listen(4)
    print(f"Bridge listening on TCP port {TCP_PORT} -> CP210x @ {BAUD} 8N1")
    try:
        while True:
            conn, addr = srv.accept()
            threading.Thread(target=handle_client, args=(conn, addr, dev), daemon=True).start()
    except KeyboardInterrupt:
        print("Stopping bridge...")
    finally:
        stop.set()
        srv.close()
        with clients_lock:
            for c in list(clients):
                try:
                    c.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        reader.join(timeout=1)          # read timeout is 200 ms
        try:
            usb.util.release_interface(dev, 0)
        except usb.core.USBError:
            pass
        usb.util.dispose_resources(dev)
        print("Bridge stopped.")


if __name__ == "__main__":
    main()
