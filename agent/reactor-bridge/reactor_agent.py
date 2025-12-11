#!/usr/bin/env python3
"""
Simple reactor agent for ESP32 FireBeetle.

Talks binary protocol over USB serial.
Prints telemetry from the board.
Lets you send SCRAM / RESET / SET_POWER commands.
"""

import sys
import threading
import time
import struct

import serial

from protocol import (
    FRAME_START_BYTE,
    MSG_TYPE_TELEMETRY,
    MSG_TYPE_COMMAND,
    CMD_ID_SCRAM,
    CMD_ID_RESET_NORMAL,
    CMD_ID_SET_POWER,
    calc_checksum,
    FrameParser,
    decode_telemetry,
)


class ReactorAgent:
    def __init__(self, port: str, baud: int = 115200):
        self.port = port
        self.baud = baud
        self.ser = serial.Serial(port, baudrate=baud, timeout=0.1)
        self.lock = threading.Lock()
        self.parser = FrameParser(self.handle_frame)
        self._stop = threading.Event()

    def start(self):
        t = threading.Thread(target=self._reader_loop, daemon=True)
        t.start()

    def close(self):
        self._stop.set()
        try:
            self.ser.close()
        except Exception:
            pass

    def _reader_loop(self):
        print(f"[agent] Reader loop started on {self.port} @ {self.baud}")
        while not self._stop.is_set():
            try:
                data = self.ser.read(64)
                if data:
                    self.parser.feed(data)
            except serial.SerialException as e:
                print(f"[agent] SerialException: {e}")
                time.sleep(1.0)

    def send_frame(self, msg_type: int, payload: bytes):
        length = len(payload)
        checksum = calc_checksum(msg_type, length, payload)
        frame = bytes([FRAME_START_BYTE, msg_type, length]) + payload + bytes([checksum])
        with self.lock:
            self.ser.write(frame)

    # --- incoming frames -------------------------------------------------

    def handle_frame(self, msg_type: int, payload: bytes):
        if msg_type == MSG_TYPE_TELEMETRY:
            self.handle_telemetry(payload)
        else:
            print(f"[agent] Unhandled msg_type=0x{msg_type:02X}, len={len(payload)}")

    def handle_telemetry(self, payload: bytes):
        data = decode_telemetry(payload)
        if not data:
            return

        print(
            f"[TELEM] sample={data['sample_id']:6d}  "
            f"temp={data['temp_c']:6.1f} C  "
            f"accel={data['accel_mag']:5.2f} g  "
            f"state={data['state_name']:7s}  "
            f"power={data['power']:3d} %"
        )

    # --- outgoing commands -----------------------------------------------

    def send_scram(self):
        payload = struct.pack("<B", CMD_ID_SCRAM)
        self.send_frame(MSG_TYPE_COMMAND, payload)
        print("[agent] Sent SCRAM command")

    def send_reset_normal(self):
        payload = struct.pack("<B", CMD_ID_RESET_NORMAL)
        self.send_frame(MSG_TYPE_COMMAND, payload)
        print("[agent] Sent RESET_NORMAL command")

    def send_set_power(self, value: int):
        value = max(0, min(100, int(value)))
        payload = struct.pack("<Bi", CMD_ID_SET_POWER, value)
        self.send_frame(MSG_TYPE_COMMAND, payload)
        print(f"[agent] Sent SET_POWER={value}")


def main(argv):
    if len(argv) < 2:
        print("Usage: reactor_agent.py SERIAL_PORT [BAUD]")
        return 1

    port = argv[1]
    baud = int(argv[2]) if len(argv) >= 3 else 115200

    agent = ReactorAgent(port, baud)
    agent.start()

    print("Simple CLI:")
    print("  scram              -> emergency shutdown")
    print("  reset              -> reset to NORMAL")
    print("  power N            -> set power to N (0..100)")
    print("  quit / exit / q    -> exit")

    try:
        while True:
            try:
                line = input("command> ").strip()
            except EOFError:
                break

            if not line:
                continue

            if line in ("quit", "exit", "q"):
                break
            elif line == "scram":
                agent.send_scram()
            elif line == "reset":
                agent.send_reset_normal()
            elif line.startswith("power"):
                parts = line.split()
                if len(parts) != 2:
                    print("Usage: power N   (N = 0..100)")
                    continue
                try:
                    value = int(parts[1])
                except ValueError:
                    print("N must be an integer")
                    continue
                agent.send_set_power(value)
            else:
                print("Unknown command. Valid: scram, reset, power N, quit")

    finally:
        print("[agent] shutting down")
        agent.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
