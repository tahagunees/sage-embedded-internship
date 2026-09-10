"""Version 3 packets include the STM32 current target for plotting."""
import binascii
from dataclasses import dataclass
import secrets
import struct
from .motor import Drive, Motor

SYNC = b'\x5a\xa5'
VERSION = 3
COMMAND = 0x10
FEEDBACK = 0x11
CMD = struct.Struct('<IIIHBBBBi')  # ... reserved, target current in mA
FB = struct.Struct('<IIQiiiiiiiiBBBBII')
LENGTHS = {COMMAND: CMD.size, FEEDBACK: FB.size}
MAX_SIZE = 8 + max(LENGTHS.values())


def frame(kind, payload):
    body = struct.pack('<BBH', VERSION, kind, len(payload)) + payload
    return SYNC + body + struct.pack('<H', binascii.crc_hqx(body, 0xffff))


class Parser:
    def __init__(self):
        self.buffer = bytearray()
        self.errors = 0

    def feed(self, data):
        frames = []
        # Byte-at-a-time accumulation bounds memory, even with a huge input chunk.
        for byte in data:
            self.buffer.append(byte)
            while self.buffer:
                if self.buffer[0] != SYNC[0]:
                    del self.buffer[0]
                    continue
                if len(self.buffer) < 2:
                    break
                if self.buffer[:2] != SYNC:
                    del self.buffer[0]
                    continue
                if len(self.buffer) < 6:
                    break
                version, kind, length = struct.unpack_from('<BBH', self.buffer, 2)
                if version != VERSION or LENGTHS.get(kind) != length:
                    self.errors += 1
                    del self.buffer[0]
                    continue
                size = length + 8
                if len(self.buffer) < size:
                    break
                body = bytes(self.buffer[2:size - 2])
                if binascii.crc_hqx(body, 0xffff) != struct.unpack_from('<H', self.buffer, size - 2)[0]:
                    self.errors += 1
                    del self.buffer[0]
                    continue
                frames.append((kind, bytes(self.buffer[6:size - 2])))
                del self.buffer[:size]
        return frames


def i32(value):
    return max(-2147483648, min(2147483647, round(value)))


def encode_command(sequence, session, drive, dt_us=1000, target_current=0.0):
    drive.validate()
    flags = int(drive.enabled) | (int(drive.reverse) << 1) | (int(drive.brake) << 2)
    return frame(COMMAND, CMD.pack(sequence, session, dt_us, round(drive.duty * 10000),
                                   drive.sector, flags, int(drive.switched), 0,
                                   i32(target_current * 1000)))


def decode_feedback(payload):
    values = FB.unpack(payload)
    names = ('sequence', 'session', 'time_us', 'ia_ma', 'ib_ma', 'ic_ma', 'current_ma',
             'bus_current_ma', 'rpm_x10', 'encoder', 'torque_unm', 'hall', 'sector',
             'encoder_ab', 'status', 'rx_errors', 'duplicates')
    return dict(zip(names, values))


@dataclass
class Endpoint:
    motor: Motor
    period_us: int = 1000

    def __post_init__(self):
        self.session = secrets.randbits(32) or 1
        self.sequence = None
        self.last_payload = None
        self.last_reply = None
        self.errors = self.duplicates = self.lost = 0
        self.timed_out = False
        self.target_current = float('nan')

    def reply(self):
        s = self.motor.snapshot()
        flags = 16 | (int(self.sequence is not None) if not self.timed_out else 0)
        flags |= int(self.motor.drive.enabled) << 1
        flags |= int(bool(self.motor.fault)) << 2
        flags |= int(self.timed_out) << 3
        flags |= int(self.motor.locked) << 6
        enc = (s['encoder'] + 2**31) % 2**32 - 2**31
        values = (self.sequence if self.sequence is not None else 0xffffffff, self.session,
                  round(s['t'] * 1e6), *(i32(s[k] * 1000) for k in ('ia', 'ib', 'ic', 'measured', 'bus_current')),
                  i32(s['rpm'] * 10), enc, i32(s['torque'] * 1e6), s['hall'], s['sector'],
                  s['encoder_ab'], flags, min(self.errors, 0xffffffff), min(self.duplicates, 0xffffffff))
        return frame(FEEDBACK, FB.pack(*values))

    def accept(self, payload):
        if len(payload) != CMD.size:
            self.errors += 1
            return None
        seq, session, dt_us, duty, sector, flags, mode, reserved, target_ma = CMD.unpack(payload)
        if (session != self.session or dt_us != self.period_us or duty > 10000 or sector > 6 or
                flags & ~7 or mode > 1 or reserved or not 0 <= target_ma <= 20000):
            self.errors += 1
            return None
        if self.sequence is not None:
            delta = (seq - self.sequence) & 0xffffffff
            if delta == 0:
                if payload == self.last_payload:
                    self.duplicates += 1
                    return self.last_reply  # idempotent retransmission; no second model step
                self.errors += 1
                return None
            if delta >= 0x80000000:
                self.errors += 1
                return None
            # Lockstep requires the NEXT command: gaps must not silently skip physics.
            if delta != 1:
                self.lost += delta - 1
                self.errors += 1
                return None
        drive = Drive(duty / 10000, sector, bool(flags & 1), bool(flags & 2), bool(mode), bool(flags & 4))
        self.motor.step(drive, dt_us / 1e6)
        self.sequence = seq
        self.target_current = target_ma / 1000
        self.last_payload = payload
        self.timed_out = False
        self.last_reply = self.reply()
        return self.last_reply
