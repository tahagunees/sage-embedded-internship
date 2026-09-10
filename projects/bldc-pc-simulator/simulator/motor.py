"""Wye-connected, non-salient BLDC; signed abc currents and diode complementarity.

SI units. Ke is PHASE peak back EMF per MECHANICAL rad/s, not line-line Ke.
The plant never chooses its own commutation sector or runs a PI controller.
"""
from dataclasses import dataclass, asdict
from itertools import product
import math

TAU = 2 * math.pi
HALL = (1, 5, 4, 6, 2, 3)
PAIRS = ((0, 1), (0, 2), (1, 2), (1, 0), (2, 0), (2, 1))


@dataclass(frozen=True)
class Parameters:
    vbus: float = 24.0
    r_phase: float = 0.30
    l_phase: float = 0.015
    ke_phase: float = 0.025
    pole_pairs: int = 7
    inertia: float = 0.002
    viscous: float = 0.0002
    friction: float = 0.001
    load_torque: float = 0.04
    encoder_cpr: int = 16384
    sensor_tau: float = 0.0002
    sensor_offset: float = 0.0
    diode_drop: float = 0.7
    max_step: float = 0.00005
    pwm_hz: float = 20000.0
    initial_electrical_deg: float = 30.0
    current_trip: float = 80.0

    def validate(self):
        for key, value in asdict(self).items():
            if not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError(f"{key}: sonlu bir sayı gerekli")
        for name in ('vbus', 'r_phase', 'l_phase', 'inertia', 'max_step', 'pwm_hz', 'current_trip'):
            if getattr(self, name) <= 0:
                raise ValueError(f"{name} > 0 olmalı")
        for name in ('ke_phase', 'viscous', 'friction', 'load_torque', 'sensor_tau', 'diode_drop'):
            if getattr(self, name) < 0:
                raise ValueError(f"{name} negatif olamaz")
        for name in ('pole_pairs', 'encoder_cpr'):
            value = getattr(self, name)
            if not isinstance(value, int) or isinstance(value, bool) or not 1 <= value <= 1000000:
                raise ValueError(f"{name}: pozitif tamsayı gerekli")
        if not 1e-7 <= self.max_step <= 0.001:
            raise ValueError('İç adım 0.1–1000 us aralığında olmalı')


@dataclass(frozen=True)
class Drive:
    duty: float = 0.0  # fraction 0..1
    sector: int = 1
    enabled: bool = False
    reverse: bool = False
    switched: bool = False
    brake: bool = False

    def validate(self):
        if not math.isfinite(self.duty) or not 0 <= self.duty <= 1:
            raise ValueError('Duty 0..1 aralığında olmalı')
        if not isinstance(self.sector, int) or not 0 <= self.sector <= 6:
            raise ValueError('Sektör 0..6 aralığında olmalı')


def trapezoid(theta):
    """+1 plateau 0..120°, -1 plateau 180..300°, 60° linear transitions."""
    x = (theta % TAU) * 180 / math.pi
    if x < 120:
        return 1.0
    if x < 180:
        return 1 - (x - 120) / 30
    if x < 300:
        return -1.0
    return -1 + (x - 300) / 30


def phase_shape(theta):
    return tuple(trapezoid(theta - j * TAU / 3) for j in range(3))


class Motor:
    def __init__(self, parameters=None):
        self.p = parameters or Parameters()
        self.p.validate()
        self.t = 0.0
        self.theta = math.radians(self.p.initial_electrical_deg) / self.p.pole_pairs
        self.encoder_origin = self.theta
        self.omega = 0.0
        self.i = [0.0, 0.0, 0.0]
        self.emf = [0.0, 0.0, 0.0]
        self.voltage = [0.0, 0.0, 0.0]
        self.neutral = 0.0
        self.torque = 0.0
        self.measured = 0.0
        self.bus_current = 0.0
        self.locked = False
        self.fault = ''
        self.drive = Drive()
        self.sense_pair = (0, 1)

    @property
    def sector(self):
        return int((self.theta * self.p.pole_pairs % TAU) / (TAU / 6)) + 1

    @property
    def encoder(self):
        return math.floor((self.theta - self.encoder_origin) / TAU * self.p.encoder_cpr)

    def snapshot(self):
        count = self.encoder
        return dict(t=self.t, ia=self.i[0], ib=self.i[1], ic=self.i[2],
                    measured=self.measured, bus_current=self.bus_current,
                    rpm=self.omega * 60 / TAU, torque=self.torque,
                    theta_deg=math.degrees(self.theta), encoder=count,
                    encoder_ab=(0, 1, 3, 2)[count % 4], hall=HALL[self.sector - 1],
                    sector=self.sector, duty=self.drive.duty * 100,
                    ea=self.emf[0], eb=self.emf[1], ec=self.emf[2],
                    fault=self.fault, locked=self.locked)

    def _circuit(self, terminals, emf, h):
        """Backward-Euler MNA with floating neutral and ideal rail diodes.

        Floating winding terminals satisfy i=0 if inside the two diode rails.
        Otherwise a rail diode conducts. Enumerate at most 3^3 active sets;
        sign constraints enforce diode direction. No artificial current clamp.
        """
        p = self.p
        dl = h / p.l_phase
        denominator = 1 + dl * p.r_phase
        opens = [k for k, v in enumerate(terminals) if v is None]
        # Prioritize fully floating, then low and high diode clamps.
        for modes in product((0, -1, 1), repeat=len(opens)):
            rail = dict(zip(opens, modes))
            v = list(terminals)
            for j, mode in rail.items():
                if mode:
                    v[j] = -p.diode_drop if mode == -1 else p.vbus + p.diode_drop
            active = [j for j in range(3) if v[j] is not None]
            if active:
                vn = sum(self.i[j] / dl + v[j] - emf[j] for j in active) / len(active)
            else:
                # All terminals open, i_new=0. Choose a feasible common mode.
                lower = max(-p.diode_drop - emf[j] + self.i[j] / dl for j in range(3))
                upper = min(p.vbus + p.diode_drop - emf[j] + self.i[j] / dl for j in range(3))
                if lower > upper + 1e-9:
                    continue
                vn = (lower + upper) / 2
            new_i = [(self.i[j] + dl * (v[j] - vn - emf[j])) / denominator
                     if v[j] is not None else 0.0 for j in range(3)]
            valid = True
            for j, mode in rail.items():
                if mode == 0:
                    v[j] = vn + emf[j] - self.i[j] / dl
                    valid &= -p.diode_drop - 1e-8 <= v[j] <= p.vbus + p.diode_drop + 1e-8
                elif mode == -1:
                    valid &= new_i[j] >= -1e-8
                else:
                    valid &= new_i[j] <= 1e-8
            if valid:
                return new_i, v, vn, rail
        raise ArithmeticError('İnverter diyot çözümü bulunamadı; parametre/adım kontrolü gerekli')

    def step(self, drive, dt):
        drive.validate()
        if not math.isfinite(dt) or not 0 < dt <= 0.1:
            raise ValueError('Adım 0..0.1 s aralığında olmalı')
        self.drive = drive
        remaining = dt
        while remaining > 1e-12:
            h = min(remaining, self.p.max_step)
            gate = drive.duty
            if drive.switched:
                period = 1 / self.p.pwm_hz
                phase = self.t % period
                # Split at carrier edges; don't alias PWM with the solver step.
                boundary = drive.duty * period
                gate = 1.0 if phase < boundary - 1e-12 else 0.0
                edge = boundary if gate else period
                h = min(h, period / 20, max(1e-10, edge - phase))
            self._substep(drive, h, gate)
            remaining -= h

    def _substep(self, drive, h, gate):
        p = self.p
        shape = phase_shape(self.theta * p.pole_pairs)
        emf = [p.ke_phase * self.omega * f for f in shape]
        terminals = [None, None, None]
        positive = negative = None
        if drive.enabled and not self.fault:
            if drive.brake:
                terminals = [0.0] * 3
            elif drive.sector:
                positive, negative = PAIRS[drive.sector - 1]
                if drive.reverse:
                    positive, negative = negative, positive
                terminals[positive] = p.vbus * gate
                terminals[negative] = 0.0
        self.i, self.voltage, self.neutral, rails = self._circuit(terminals, emf, h)
        self.emf = emf
        self.torque = p.ke_phase * sum(f * i for f, i in zip(shape, self.i))
        self.bus_current = (gate * self.i[positive] if positive is not None else 0.0)
        self.bus_current += sum(self.i[j] for j, mode in rails.items() if mode == 1)
        # Scalar measurement = conducting pair current, not averaged DC-bus current.
        if positive is not None:
            self.sense_pair = (positive, negative)
        a, b = self.sense_pair
        raw = (self.i[a] - self.i[b]) / 2
        self.measured += h / (p.sensor_tau + h) * (raw + p.sensor_offset - self.measured)
        old_speed = self.omega
        resisting = p.friction + p.load_torque
        if self.locked:
            self.omega = 0.0
        elif abs(old_speed) < 1e-10 and abs(self.torque) <= resisting:
            self.omega = 0.0
        else:
            sign = math.copysign(1, old_speed if abs(old_speed) > 1e-10 else self.torque)
            speed = (old_speed + h / p.inertia * (self.torque - sign * resisting)) / (1 + h * p.viscous / p.inertia)
            if speed * old_speed < 0 and abs(self.torque) <= resisting:
                speed = 0.0
            self.omega = speed
        if not self.locked:
            self.theta += (old_speed + self.omega) * h / 2
        self.t += h
        if max(abs(i) for i in self.i) > p.current_trip:
            self.fault = 'Faz aşırı akımı: inverter açık devreye alındı'
        if not all(math.isfinite(v) for v in (*self.i, self.omega, self.theta, self.measured)):
            raise ArithmeticError('Sonlu olmayan model durumu')
