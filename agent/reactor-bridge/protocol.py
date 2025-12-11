import struct

FRAME_START_BYTE = 0xAA

MSG_TYPE_TELEMETRY = 0x01
MSG_TYPE_COMMAND   = 0x10

CMD_ID_SCRAM        = 1
CMD_ID_RESET_NORMAL = 2
CMD_ID_SET_POWER    = 3

STATE_NAMES = {
    0: "NORMAL",
    1: "WARNING",
    2: "SCRAM",
}


def calc_checksum(msg_type: int, length: int, payload: bytes) -> int:
    c = msg_type ^ length
    for b in payload:
        c ^= b
    return c & 0xFF


class FrameParser:
    """
    Stateful frame parser for:
      [0xAA][MSG_TYPE][LENGTH][PAYLOAD...][CHECKSUM]
    """

    def __init__(self, callback):
        self.callback = callback
        self.state = "WAIT_START"
        self.msg_type = 0
        self.length = 0
        self.payload = bytearray()
        self.checksum = 0

    def feed(self, data: bytes):
        for b in data:
            self._process_byte(b)

    def _process_byte(self, b: int):
        if self.state == "WAIT_START":
            if b == FRAME_START_BYTE:
                self.state = "READ_TYPE"

        elif self.state == "READ_TYPE":
            self.msg_type = b
            self.checksum = self.msg_type
            self.state = "READ_LENGTH"

        elif self.state == "READ_LENGTH":
            self.length = b
            self.checksum ^= self.length
            if self.length == 0:
                self.payload = bytearray()
                self.state = "READ_CHECKSUM"
            elif self.length > 64:
                print(f"[parser] length too large: {self.length}, resetting")
                self.state = "WAIT_START"
            else:
                self.payload = bytearray()
                self.state = "READ_PAYLOAD"

        elif self.state == "READ_PAYLOAD":
            self.payload.append(b)
            self.checksum ^= b
            if len(self.payload) >= self.length:
                self.state = "READ_CHECKSUM"

        elif self.state == "READ_CHECKSUM":
            if self.checksum == b:
                try:
                    self.callback(self.msg_type, bytes(self.payload))
                except Exception as e:
                    print(f"[parser] error in callback: {e}")
            else:
                print(f"[parser] checksum error "
                      f"(expected 0x{self.checksum:02X}, got 0x{b:02X})")
            self.state = "WAIT_START"

        else:
            self.state = "WAIT_START"


def decode_telemetry(payload: bytes):
    """Return nicely named telemetry fields, or None on error."""
    if len(payload) != 14:
        print(f"[agent] telemetry payload wrong length: {len(payload)}")
        return None

    sample_id, temp_c, accel_mag, state_raw, power = struct.unpack(
        "<IffBB", payload
    )
    state_name = STATE_NAMES.get(state_raw, f"UNKNOWN({state_raw})")

    return {
        "sample_id": sample_id,
        "temp_c": temp_c,
        "accel_mag": accel_mag,
        "state_raw": state_raw,
        "state_name": state_name,
        "power": power,
    }
