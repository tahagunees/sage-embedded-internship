"""Educational motor model; no hardware or serial dependency."""
from dataclasses import dataclass


@dataclass
class PidMotor:
    target: float = 90.0
    kp: float = 2.0
    ki: float = 0.0
    kd: float = 1.0
    damping: float = 0.8
    load: float = 0.0
    angle: float = 0.0
    speed: float = 0.0
    integral: float = 0.0
    time: float = 0.0

    def advance(self, dt=0.005):
        error = self.target - self.angle
        p = self.kp * error
        # Derivative on measured velocity avoids a target-change kick.
        d = -self.kd * self.speed
        candidate = self.integral + self.ki * error * dt if self.ki else 0.0
        proposed = p + candidate + d
        # Conditional integration: do not accumulate further into saturation.
        if (abs(proposed) <= 100 or proposed > 100 and error < 0
                or proposed < -100 and error > 0 or not self.ki):
            self.integral = candidate
        output = max(-100.0, min(100.0, p + self.integral + d))
        snapshot = (self.time, self.target, self.angle, error, p,
                    self.integral, d, output)
        # Unit inertia and gain 4: speed in deg/s, load in output units.
        acceleration = 4.0 * (output - self.load) - self.damping * self.speed
        self.speed += acceleration * dt
        self.angle += self.speed * dt
        self.time += dt
        return snapshot

    def reset(self):
        self.angle = self.speed = self.integral = self.time = 0.0
