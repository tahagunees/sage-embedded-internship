import os
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
from PyQt6.QtWidgets import QApplication
from PyQt6.QtGui import QFontDatabase
from ui.pid_simulator import PidSimulator
from ui.theme import DARK_STYLESHEET
app = QApplication([])
QFontDatabase.addApplicationFont('C:/Windows/Fonts/segoeui.ttf')
app.setStyle('Fusion')
app.setStyleSheet(DARK_STYLESHEET + '\nQWidget { font-family: "Segoe UI"; }')
window = PidSimulator()
window.show()
window.preset((2, .8, 1, 15))
for _ in range(650):
    window.tick()
assert len(window.samples) == 650
assert abs(window.motor.angle - 90) < 1
window.toggle()
assert window.timer.isActive()
window.toggle()
assert not window.timer.isActive()
app.processEvents()
window.grab().save('pid-ui-preview.png')
window.reset()
assert len(window.samples) == 0
window.toggle()
window.reject()
assert not window.timer.isActive()
print('PASS: PID UI controls, history, pause, reset, close')
