#!/usr/bin/env python3
"""
Read this board's USB-CDC serial output.

Why this exists instead of a one-line shell command: on this ESP32-S3 the
native USB-CDC port only emits once the host asserts DTR. `cat /dev/cu.*`
never raises DTR, and `arduino-cli monitor` doesn't either — both sit there
printing nothing while the board is running fine, which looks exactly like a
crashed sketch. This opens the port and raises DTR first.

Usage:
    python3 tools/serial_monitor.py [port] [seconds]

Defaults to /dev/cu.usbmodem201101 and runs until Ctrl-C. Find the port with
`arduino-cli board list`.

Note you will usually still miss setup()'s output: the port re-enumerates on
reset, so a reader attaching afterwards has already missed it. Sketches here
reprint their diagnostics from loop() for that reason.
"""
import fcntl
import os
import struct
import sys
import termios
import time

TIOCMBIS = 0x8004746C
TIOCM_DTR = 0x002

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem201101"
limit = float(sys.argv[2]) if len(sys.argv) > 2 else None

fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    attrs = termios.tcgetattr(fd)
    attrs[0] = attrs[1] = attrs[3] = 0          # raw: no in/out/local processing
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    fcntl.ioctl(fd, TIOCMBIS, struct.pack("I", TIOCM_DTR))  # the part that matters

    start = time.time()
    while limit is None or time.time() - start < limit:
        try:
            chunk = os.read(fd, 4096)
            if chunk:
                sys.stdout.write(chunk.decode(errors="replace"))
                sys.stdout.flush()
        except BlockingIOError:
            pass
        time.sleep(0.03)
except KeyboardInterrupt:
    pass
finally:
    os.close(fd)
