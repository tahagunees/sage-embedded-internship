"""Encoder feedback and local-only proportional-control learning panel."""
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import QDoubleSpinBox, QFormLayout, QGroupBox, QLabel, QPushButton


class PositionPanel(QGroupBox):
    def __init__(self, parent=None):
        super().__init__("ENCODER / P PREVIEW", parent)
        layout = QFormLayout(self)
        layout.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
        self.target = QDoubleSpinBox()
        self.target.setRange(-1000000, 1000000)
        self.target.setDecimals(2)
        self.target.setSuffix(" °")
        self.target.setValue(90)
        self.kp = QDoubleSpinBox()
        self.kp.setRange(0, 1000)
        self.kp.setDecimals(3)
        self.kp.setSingleStep(0.1)
        self.kp.setValue(2)
        self.count = QLabel("—")
        self.angle = QLabel("—")
        self.rpm = QLabel("—")
        self.error = QLabel("—")
        self.output = QLabel("—")
        self.status = QLabel("Waiting for encoder")
        self.status.setWordWrap(True)
        note = QLabel("Local calculation only · no motor command.\n"
                      "u = Kp × (target − angle), limited to ±100 units.\n"
                      "Uses the encoder angle as received; no 360° wrapping.")
        note.setWordWrap(True)
        for name, widget in (("Encoder count", self.count), ("Angle", self.angle),
                             ("Encoder RPM", self.rpm), ("Target angle", self.target),
                             ("Kp", self.kp), ("Position error", self.error),
                             ("P output", self.output)):
            layout.addRow(name, widget)
        layout.addRow(self.status)
        layout.addRow(note)
        simulator_button = QPushButton("Open PID Simulation")
        simulator_button.clicked.connect(self.open_simulator)
        layout.addRow(simulator_button)
        self._simulator = None
        self._sample = None
        self._received = 0.0
        self.target.valueChanged.connect(self.refresh)
        self.kp.valueChanged.connect(self.refresh)
        self._timer = QTimer(self)
        self._timer.timeout.connect(self.refresh)
        self._timer.start(250)

    def open_simulator(self):
        from ui.pid_simulator import PidSimulator
        if self._simulator is None:
            self._simulator = PidSimulator(self)
        self._simulator.show()
        self._simulator.raise_()
        self._simulator.activateWindow()

    def accept(self, sample):
        self._sample = sample
        self._received = time.monotonic()
        self.count.setText(str(sample.count))
        self.angle.setText(f"{sample.degrees:g} °")
        self.rpm.setText(f"{sample.rpm:g} rpm")
        self.refresh()

    def reset(self):
        self._sample = None
        for label in (self.count, self.angle, self.rpm, self.error, self.output):
            label.setText("—")
        self.status.setText("Waiting for encoder")

    def refresh(self, *_):
        if self._sample is None:
            return
        if time.monotonic() - self._received > 2:
            self.status.setText("Encoder data stale (>2 s); last measurement shown")
            self.error.setText("—")
            self.output.setText("—")
            return
        error = self.target.value() - self._sample.degrees
        raw = self.kp.value() * error
        output = max(-100.0, min(100.0, raw))
        self.error.setText(f"{error:+.2f} °")
        self.output.setText(f"{output:+.2f}" + (" (limited)" if output != raw else ""))
        self.status.setText("Encoder data current · preview only")
