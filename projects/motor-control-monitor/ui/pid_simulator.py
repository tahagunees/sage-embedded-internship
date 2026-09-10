"""Local PID laboratory. Never creates a serial connection."""
from collections import deque

import pyqtgraph as pg
from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (QDialog, QDoubleSpinBox, QFormLayout, QHBoxLayout,
                             QLabel, QPushButton, QVBoxLayout, QWidget)
from models.pid_simulation import PidMotor


class PidSimulator(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("PID Simulation · Virtual motor")
        self.resize(1160, 780)
        self.motor = PidMotor()
        self.samples = deque(maxlen=6000)
        root = QHBoxLayout(self)
        controls = QWidget()
        controls.setMaximumWidth(285)
        form = QFormLayout(controls)
        title = QLabel("VIRTUAL MOTOR\nNo commands are sent to STM32.")
        title.setWordWrap(True)
        form.addRow(title)
        self.inputs = {}
        for key, label, low, high, value in (
                ('target', 'Target (°)', -720, 720, 90),
                ('kp', 'Kp', 0, 30, 2), ('ki', 'Ki', 0, 20, 0),
                ('kd', 'Kd', 0, 20, 1), ('damping', 'Friction / damping', 0, 10, .8),
                ('load', 'Constant load', -50, 50, 0)):
            box = QDoubleSpinBox()
            box.setRange(low, high)
            box.setDecimals(2)
            box.setSingleStep(1 if key in ('target', 'load') else .1)
            box.setValue(value)
            box.valueChanged.connect(lambda v, name=key: self.set_parameter(name, v))
            self.inputs[key] = box
            form.addRow(label, box)
        self.start_button = QPushButton('Start')
        self.start_button.clicked.connect(self.toggle)
        reset = QPushButton('Reset motion')
        reset.clicked.connect(self.reset)
        form.addRow(self.start_button, reset)
        for text, settings in (
                ('1 · P only', (2, 0, 0, 0)),
                ('2 · PD: reduce overshoot', (2, 0, 1, 0)),
                ('3 · PID: remove load error', (2, .8, 1, 15))):
            button = QPushButton(text)
            button.clicked.connect(lambda _, values=settings: self.preset(values))
            form.addRow(button)
        self.readout = QLabel('Ready · press Start')
        self.readout.setWordWrap(True)
        form.addRow(self.readout)
        note = QLabel('P = Kp × error\nI accumulates error over time\n'
                      'D = −Kd × measured speed\n\n'
                      'Output limited to ±100 model units. Anti-windup enabled.\n'
                      'Fixed 5 ms model step; slow UI rendering slows simulation time.\n\n'
                      'Simplified inertia model, not calibrated to your motor. '
                      'Angles are continuous, not wrapped at 360°.\n\n'
                      'Try adding load in P mode, then increase Ki to remove the offset.')
        note.setWordWrap(True)
        form.addRow(note)
        root.addWidget(controls)
        charts = QVBoxLayout()
        root.addLayout(charts, 1)
        self.curves = []
        for title, ylabel, series in (
                ('Target and motor position', 'Angle (°)', ((1, 'Target', '#f0a020'), (2, 'Motor', '#3fb950'))),
                ('Position error', 'Error (°)', ((3, 'Error', '#58a6ff'),)),
                ('PID contributions and limited output', 'Model units',
                 ((4, 'P', '#58a6ff'), (5, 'I', '#a371f7'), (6, 'D', '#f0a020'), (7, 'Output', '#3fb950')))):
            plot = pg.PlotWidget(title=title)
            plot.setBackground('#0b0f14')
            plot.setLabel('left', ylabel)
            plot.setLabel('bottom', 'Simulation time', units='s')
            plot.showGrid(x=True, y=True, alpha=.2)
            plot.addLegend()
            for index, name, color in series:
                self.curves.append((plot, index, plot.plot(name=name, pen=pg.mkPen(color, width=2))))
            charts.addWidget(plot)
        self.timer = QTimer(self)
        self.timer.setInterval(20)
        self.timer.timeout.connect(self.tick)

    def set_parameter(self, name, value):
        setattr(self.motor, name, value)
        if name == 'ki':
            self.motor.integral = 0.0

    def toggle(self):
        if self.timer.isActive():
            self.timer.stop()
            self.start_button.setText('Resume')
        else:
            self.timer.start()
            self.start_button.setText('Pause')

    def reset(self):
        self.timer.stop()
        self.start_button.setText('Start')
        self.motor.reset()
        self.samples.clear()
        for _, _, curve in self.curves:
            curve.setData([], [])
        self.readout.setText('Reset to 0° · press Start')

    def preset(self, values):
        self.reset()
        for key, value in zip(('kp', 'ki', 'kd', 'load'), values):
            self.inputs[key].setValue(value)
        self.inputs['target'].setValue(90)
        self.inputs['damping'].setValue(.8)

    def tick(self):
        for _ in range(4):
            sample = self.motor.advance()
        self.samples.append(sample)
        rows = list(self.samples)
        times = [row[0] for row in rows]
        for plot, index, curve in self.curves:
            curve.setData(times, [row[index] for row in rows])
            plot.setXRange(max(0, sample[0] - 20), max(20, sample[0]), padding=0)
        self.readout.setText(
            f'Time: {sample[0]:.2f} s\nAngle: {sample[2]:.2f}°\n'
            f'Error: {sample[3]:+.2f}°\nP: {sample[4]:+.2f}\n'
            f'I: {sample[5]:+.2f}\nD: {sample[6]:+.2f}\n'
            f'Output: {sample[7]:+.2f}')

    def done(self, result):
        self.timer.stop()
        self.start_button.setText('Resume')
        super().done(result)

    def closeEvent(self, event):
        self.timer.stop()
        self.start_button.setText('Resume')
        super().closeEvent(event)
