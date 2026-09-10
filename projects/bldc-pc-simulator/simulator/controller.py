"""Demo controller only. UART mode never calls this class.

PI update follows current_pi_sim.c from the user-selected STM32 project:
10 ms, Kp=2 %/A, Ki=20 %/(A s), conditional-integration antiwindup.
The old embedded scalar plant is intentionally not part of this controller.
"""
from dataclasses import dataclass
from .motor import Drive


@dataclass
class DemoPI:
    target: float = 3.0
    kp: float = 2.0
    ki: float = 20.0
    integral: float = 0.0
    duty: float = 0.0
    enabled: bool = True
    reverse: bool = False
    switched: bool = False
    next_update: float = 0.0
    period: float = 0.01

    def command(self, motor):
        if motor.t + 1e-10 >= self.next_update:
            self.next_update = motor.t + self.period
            error = self.target - motor.measured
            p = self.kp * error
            increment = self.ki * error * self.period
            unsaturated = p + self.integral
            if not self.enabled or motor.fault:
                self.integral = self.duty = 0.0
            else:
                if (unsaturated < 100 or increment < 0) and (unsaturated > 0 or increment > 0):
                    self.integral = min(100, max(0, self.integral + increment))
                self.duty = min(100, max(0, p + self.integral)) / 100
        return Drive(self.duty, motor.sector, self.enabled, self.reverse, self.switched)
