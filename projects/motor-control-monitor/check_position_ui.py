import os
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
from PyQt6.QtWidgets import QApplication
from PyQt6.QtGui import QFontDatabase, QFont
from ui.main_window import MainWindow
from ui.theme import DARK_STYLESHEET

app = QApplication([])
font_id = QFontDatabase.addApplicationFont('C:/Windows/Fonts/segoeui.ttf')
families = QFontDatabase.applicationFontFamilies(font_id)
if families:
    app.setFont(QFont(families[0], 10))
app.setStyle('Fusion')
app.setStyleSheet(DARK_STYLESHEET + '\nQWidget { font-family: "Segoe UI"; }')
window = MainWindow()
window.show()
window._on_serial_line('ENC: cnt=120 deg=60 rpm=12')
p = window.position_panel
assert p.count.text() == '120'
assert p.angle.text() == '60 °'
assert p.rpm.text() == '12 rpm'
assert p.error.text() == '+30.00 °'
assert p.output.text() == '+60.00'
p.target.setValue(0)
assert p.output.text() == '-100.00 (limited)'
p.target.setValue(90)
p.kp.setValue(0)
assert p.output.text() == '+0.00'
p.kp.setValue(2)
window._on_serial_line('ENC: cnt=bad deg=99 rpm=12')
assert p.angle.text() == '60 °'
p._received -= 3
p.refresh()
assert p.output.text() == '—'
window._on_serial_line('ENC: cnt=120 deg=60 rpm=12')
app.processEvents()
window.grab().save('position-ui-preview.png')
window._on_connection_closed('requested')
assert p.angle.text() == '—'
assert window._worker is None
window.close()
print('PASS: encoder fields, P error, clamp, zero gain, malformed input, stale data, disconnect; no serial connection opened')
