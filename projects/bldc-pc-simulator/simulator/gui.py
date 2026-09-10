import csv
import bisect
from collections import deque
from dataclasses import asdict, replace
import json
import math
from pathlib import Path
import queue
import time
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from .motor import Motor, Parameters
from .controller import DemoPI
from .transport import SerialWorker

BG, PANEL, TEXT, MUTED = '#101827', '#182338', '#e5edf7', '#92a5bd'
COLORS = ('#fb7185', '#4ade80', '#60a5fa', '#fbbf24')
PARAM_LABELS = {
    'vbus': 'DC bara (V)', 'r_phase': 'Faz direnci (Ω)', 'l_phase': 'Faz endüktansı (H)',
    'ke_phase': 'Ke faz tepe / mekanik rad/s', 'pole_pairs': 'Kutup çifti',
    'inertia': 'Toplam atalet J (kg·m²)', 'viscous': 'Viskoz sürtünme B (Nm·s)',
    'friction': 'Coulomb sürtünme (Nm)', 'load_torque': 'Direnen yük (Nm)',
    'encoder_cpr': 'Encoder sayım/devir (x4 dahil)', 'sensor_tau': 'Akım filtre τ (s)',
    'sensor_offset': 'Akım ölçüm ofseti (A)', 'diode_drop': 'Diyot düşümü (V)',
    'max_step': 'İç çözüm adımı (s)', 'pwm_hz': 'PWM frekansı (Hz)',
    'initial_electrical_deg': 'Başlangıç elektrik açısı (°)', 'current_trip': 'Faz koruma eşiği (A)',
}

PLOT_SPECS = (
    ('FAZ AKIMLARI · A', ('ia', 'ib', 'ic'), ('A', 'B', 'C')),
    ('AKIM KONTROLÜ · A', ('measured', 'target'), ('Ölçüm', 'Hedef')),
    ('ROTOR HIZI · RPM', ('rpm',), ('Mekanik RPM',)),
    ('ROTOR / HALL', ('sector', 'encoder_ab'), ('Sektör 1–6', 'Encoder AB 0–3')),
)


class DetailPlot:
    """Large, zoomable history view opened from a dashboard chart."""
    def __init__(self, app, spec):
        self.app = app
        self.title, self.keys, self.labels = spec
        self.window = tk.Toplevel(app.root)
        self.window.title(f'{self.title} — detay')
        self.window.geometry('1100x650')
        self.window.minsize(720, 420)
        self.window.configure(bg=BG)
        self.live = True
        self.span = 1.0
        self.end_t = None
        self.drag_x = None
        self.drag_end = None
        self.plot_geometry = None
        self.visible_rows = []

        toolbar = ttk.Frame(self.window, padding=(14, 10))
        toolbar.pack(fill='x')
        ttk.Label(toolbar, text=self.title, font=('Segoe UI', 13, 'bold')).pack(side='left')
        ttk.Button(toolbar, text='Canlıyı izle', command=self.go_live).pack(side='right', padx=(6, 0))
        ttk.Button(toolbar, text='Tüm kayıt', command=lambda: self.set_span(None)).pack(side='right', padx=3)
        for label, seconds in reversed((('100 ms', .1), ('500 ms', .5), ('1 s', 1), ('5 s', 5), ('20 s', 20))):
            ttk.Button(toolbar, text=label, command=lambda value=seconds: self.set_span(value)).pack(side='right', padx=3)

        self.canvas = tk.Canvas(self.window, bg=PANEL, highlightthickness=0)
        self.canvas.pack(fill='both', expand=True, padx=14, pady=(0, 8))
        self.readout = tk.StringVar(value='Fareyi grafik üzerinde gezdirin: anlık değerler burada görünür.')
        ttk.Label(self.window, textvariable=self.readout, padding=(14, 4), foreground='#67e8f9').pack(fill='x')
        ttk.Label(self.window,
                  text='Tekerlek: yakınlaştır · Sol tuşla sürükle: geçmişte gezin · Çift tık: canlı görünüm',
                  padding=(14, 2, 14, 10), foreground=MUTED).pack(fill='x')
        self.canvas.bind('<MouseWheel>', self.zoom)
        self.canvas.bind('<ButtonPress-1>', self.begin_pan)
        self.canvas.bind('<B1-Motion>', self.pan)
        self.canvas.bind('<Motion>', self.cursor)
        self.canvas.bind('<Leave>', lambda _event: self.draw())
        self.canvas.bind('<Double-Button-1>', lambda _event: self.go_live())
        self.window.protocol('WM_DELETE_WINDOW', self.close)
        self.refresh()

    def close(self):
        self.window.destroy()

    def go_live(self):
        self.live = True
        self.end_t = None
        self.draw()

    def set_span(self, seconds):
        self.span = seconds
        self.live = True
        self.end_t = None
        self.draw()

    def zoom(self, event):
        rows = list(self.app.records)
        if len(rows) < 2:
            return
        total = max(rows[-1]['t'] - rows[0]['t'], .001)
        old_span = total if self.span is None else self.span
        new_span = max(.02, min(total, old_span * (.5 if event.delta > 0 else 2)))
        self.span = new_span
        self.live = False
        x0, _, x1, _ = self.plot_geometry or (70, 42, max(self.canvas.winfo_width() - 20, 71), 400)
        ratio = min(1, max(0, (event.x - x0) / max(x1 - x0, 1)))
        old_end = self.end_t if self.end_t is not None else rows[-1]['t']
        cursor_t = old_end - old_span + ratio * old_span
        self.end_t = cursor_t + (1 - ratio) * new_span
        self.draw()

    def begin_pan(self, event):
        rows = list(self.app.records)
        if not rows:
            return
        self.live = False
        self.drag_x = event.x
        self.drag_end = self.end_t if self.end_t is not None else rows[-1]['t']

    def pan(self, event):
        if self.drag_x is None or not self.app.records:
            return
        total = max(self.app.records[-1]['t'] - self.app.records[0]['t'], .001)
        span = total if self.span is None else min(self.span, total)
        width = max(self.canvas.winfo_width() - 90, 1)
        self.end_t = self.drag_end - (event.x - self.drag_x) / width * span
        self.draw()

    def _range(self):
        rows = list(self.app.records)
        if not rows:
            return [], 0, 1
        latest, earliest = rows[-1]['t'], rows[0]['t']
        end = latest if self.live or self.end_t is None else min(latest, max(earliest, self.end_t))
        span = max(latest - earliest, .001) if self.span is None else self.span
        start = max(earliest, end - span)
        times = [row['t'] for row in rows]
        shown = rows[bisect.bisect_left(times, start):bisect.bisect_right(times, end)]
        return shown, start, max(end, start + .000001)

    @staticmethod
    def _decimate(rows, width):
        limit = max(int(width) * 2, 500)
        if len(rows) <= limit:
            return rows
        step = len(rows) / limit
        return [rows[int(i * step)] for i in range(limit)]

    def refresh(self):
        if not self.window.winfo_exists():
            return
        self.draw()
        self.window.after(100, self.refresh)

    def draw(self):
        rows, t0, t1 = self._range()
        self.visible_rows = rows
        canvas = self.canvas
        canvas.delete('all')
        w, h = max(canvas.winfo_width(), 200), max(canvas.winfo_height(), 160)
        x0, y0, x1, y1 = 72, 42, w - 20, h - 55
        self.plot_geometry = (x0, y0, x1, y1)
        values = [row[key] for row in rows for key in self.keys if math.isfinite(row[key])]
        lo, hi = min(values, default=0), max(values, default=1)
        span_y = max(hi - lo, .0001)
        lo -= span_y * .1
        hi += span_y * .1
        for j in range(6):
            y = y0 + j * (y1 - y0) / 5
            canvas.create_line(x0, y, x1, y, fill='#2b3951')
            canvas.create_text(x0 - 8, y, text=f'{hi - j*(hi-lo)/5:.3f}', anchor='e', fill=MUTED,
                               font=('Segoe UI', 9))
        for j in range(6):
            x = x0 + j * (x1 - x0) / 5
            canvas.create_line(x, y0, x, y1, fill='#223149')
            canvas.create_text(x, y1 + 17, text=f'{t0 + j*(t1-t0)/5:.3f}s', fill=MUTED,
                               font=('Segoe UI', 9))
        drawn = self._decimate(rows, x1 - x0)
        for j, key in enumerate(self.keys):
            points = []
            for row in drawn:
                value = row[key]
                if math.isfinite(value):
                    points.extend((x0 + (row['t']-t0)/(t1-t0)*(x1-x0),
                                   y1 - (value-lo)/(hi-lo)*(y1-y0)))
            if len(points) >= 4:
                canvas.create_line(points, fill=COLORS[j], width=2)
            canvas.create_text(x0 + j * 180, 20, text=self.labels[j], fill=COLORS[j], anchor='w',
                               font=('Segoe UI', 10, 'bold'))
        mode = 'CANLI' if self.live else 'İNCELEME'
        canvas.create_text(x1, 20, text=f'{mode} · {len(rows)} örnek · Δt={t1-t0:.3f} s',
                           fill='#67e8f9', anchor='e', font=('Segoe UI', 9, 'bold'))
        if rows:
            stats = []
            for label, key in zip(self.labels, self.keys):
                series = [row[key] for row in rows if math.isfinite(row[key])]
                if series:
                    stats.append(f'{label}: min {min(series):.3f}  maks {max(series):.3f}  ort {sum(series)/len(series):.3f}')
            self.readout.set('     '.join(stats))

    def cursor(self, event):
        if not self.visible_rows or not self.plot_geometry:
            return
        x0, y0, x1, y1 = self.plot_geometry
        if not (x0 <= event.x <= x1 and y0 <= event.y <= y1):
            return
        first_t, last_t = self.visible_rows[0]['t'], self.visible_rows[-1]['t']
        wanted = first_t + (event.x - x0) / max(x1 - x0, 1) * (last_t - first_t)
        times = [row['t'] for row in self.visible_rows]
        index = min(bisect.bisect_left(times, wanted), len(times) - 1)
        if index and abs(times[index - 1] - wanted) < abs(times[index] - wanted):
            index -= 1
        row = self.visible_rows[index]
        x = x0 + (row['t'] - first_t) / max(last_t - first_t, .000001) * (x1 - x0)
        self.canvas.delete('cursor')
        self.canvas.create_line(x, y0, x, y1, fill='#f8fafc', dash=(3, 3), tags='cursor')
        fields = [f't={row["t"]:.6f} s']
        fields.extend(f'{label}={row[key]:.5f}' for label, key in zip(self.labels, self.keys)
                      if math.isfinite(row[key]))
        self.readout.set('     '.join(fields))


class App:
    def __init__(self, root):
        self.root = root
        root.title('BLDC Motor Laboratuvarı — PC simülatörü')
        root.geometry('1180x820')
        root.minsize(1000, 700)
        root.configure(bg=BG)
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('.', font=('Segoe UI', 10))
        style.configure('TFrame', background=BG)
        style.configure('TLabel', background=BG, foreground=TEXT)
        style.configure('TCheckbutton', background=BG, foreground=TEXT)
        style.configure('TButton', padding=(10, 6))
        self.motor = Motor()
        self.pi = DemoPI()
        self.history = deque(maxlen=3000)
        self.records = deque(maxlen=100000)
        self.detail_windows = []
        self.running = False
        self.worker = None
        self.started_wall = time.monotonic()
        self.mode = tk.StringVar(value='demo')
        self.target = tk.DoubleVar(value=3)
        self.kp = tk.DoubleVar(value=2)
        self.ki = tk.DoubleVar(value=20)
        self.locked = tk.BooleanVar(value=False)
        self.reverse = tk.BooleanVar(value=False)
        self.switched = tk.BooleanVar(value=False)
        self.port = tk.StringVar()
        self.baud = tk.StringVar(value='4000000')
        self.status = tk.StringVar(value='Hazır · DEMO PI bilgisayarda · STM32 bağlı değil')
        self._build()
        self._record(self.motor.snapshot())
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.after(50, self.tick)

    def _build(self):
        header = ttk.Frame(self.root, padding=(20, 14))
        header.pack(fill='x')
        ttk.Label(header, text='BLDC MOTOR LABORATUVARI', font=('Segoe UI', 20, 'bold')).pack(anchor='w')
        ttk.Label(header, text='Üç faz elektrik modeli  →  tork ve rotor  →  Hall / encoder', foreground=MUTED).pack(anchor='w', pady=(3, 0))
        bar = ttk.Frame(self.root, padding=(20, 4))
        bar.pack(fill='x')
        ttk.Radiobutton(bar, text='PC demo — PI burada', variable=self.mode, value='demo', command=self.change_mode).pack(side='left')
        ttk.Radiobutton(bar, text='STM32 — PI kartta / UART', variable=self.mode, value='uart', command=self.change_mode).pack(side='left', padx=15)
        ttk.Button(bar, text='Motor parametreleri', command=self.parameters).pack(side='right')
        self.start_button = ttk.Button(bar, text='Başlat / Duraklat', command=self.toggle)
        self.start_button.pack(side='right', padx=8)
        ttk.Button(bar, text='Sıfırla', command=self.reset).pack(side='right')

        controls = ttk.Frame(self.root, padding=(20, 8))
        controls.pack(fill='x')
        for label, variable in [('Hedef akım (A)', self.target), ('Kp (%/A)', self.kp), ('Ki (%/A/s)', self.ki)]:
            ttk.Label(controls, text=label).pack(side='left', padx=(0, 5))
            ttk.Entry(controls, textvariable=variable, width=7).pack(side='left', padx=(0, 14))
        ttk.Checkbutton(controls, text='Rotor kilitli', variable=self.locked).pack(side='left', padx=5)
        ttk.Checkbutton(controls, text='Ters yön', variable=self.reverse).pack(side='left', padx=5)
        ttk.Checkbutton(controls, text='PWM anahtarlamalı', variable=self.switched).pack(side='left', padx=5)
        serialbar = ttk.Frame(self.root, padding=(20, 4))
        serialbar.pack(fill='x')
        ttk.Label(serialbar, text='UART port').pack(side='left')
        self.port_box = ttk.Combobox(serialbar, textvariable=self.port, width=17)
        self.port_box.pack(side='left', padx=8)
        ttk.Button(serialbar, text='Tara', command=self.scan).pack(side='left')
        ttk.Combobox(serialbar, textvariable=self.baud, values=('115200', '921600', '4000000'), width=10).pack(side='left', padx=8)
        ttk.Button(serialbar, text='Bağlan / Ayır', command=self.connect).pack(side='left')
        ttk.Button(serialbar, text='CSV kaydet', command=self.export).pack(side='right')
        ttk.Label(serialbar, text='1 ms model alışverişi · PI örneği 10 ms', foreground=MUTED).pack(side='right', padx=12)

        self.cards = tk.Canvas(self.root, bg=BG, height=92, highlightthickness=0)
        self.cards.pack(fill='x', padx=20, pady=5)
        area = ttk.Frame(self.root, padding=(20, 0))
        area.pack(fill='both', expand=True)
        area.columnconfigure(0, weight=1)
        area.columnconfigure(1, weight=1)
        area.rowconfigure(0, weight=1)
        area.rowconfigure(1, weight=1)
        self.plots = []
        for index in range(4):
            canvas = tk.Canvas(area, bg=PANEL, highlightthickness=0, height=160)
            canvas.grid(row=index // 2, column=index % 2, sticky='nsew', padx=(0 if index % 2 == 0 else 8, 0), pady=5)
            canvas.bind('<Double-Button-1>', lambda _event, selected=index: self.open_detail(selected))
            canvas.configure(cursor='hand2')
            self.plots.append(canvas)
        ttk.Label(self.root, textvariable=self.status, padding=(20, 7), foreground='#67e8f9').pack(fill='x')
        ttk.Label(self.root, text='Grafiğe çift tıklayın: büyüt ve incele • fiziksel motor çıkışı yok • UART v3',
                  foreground=MUTED, padding=(20, 0, 20, 12)).pack(fill='x')
        self.scan()

    def open_detail(self, index):
        detail = DetailPlot(self, PLOT_SPECS[index])
        self.detail_windows.append(detail)
        detail.window.bind('<Destroy>', lambda event, item=detail:
                           self.detail_windows.remove(item)
                           if event.widget is item.window and item in self.detail_windows else None)

    def scan(self):
        try:
            from serial.tools import list_ports
            ports = [p.device for p in list_ports.comports()]
            self.port_box['values'] = ports
            if ports and not self.port.get():
                self.port.set(ports[0])
        except ImportError:
            self.status.set('UART için pyserial gerekli; PC demo hazır')

    def change_mode(self):
        self.running = False
        self.disconnect()
        self.status.set('DEMO PI bilgisayarda' if self.mode.get() == 'demo' else 'UART v3 hazır · hedef/Kp/Ki STM32 tarafında belirlenir')

    def toggle(self):
        if self.mode.get() != 'demo':
            self.status.set('UART modunda model yalnız geçerli STM32 komutuyla ilerler')
            return
        self.running = not self.running
        self.status.set('DEMO çalışıyor · STM32 PI testi değildir' if self.running else 'DEMO duraklatıldı')

    def disconnect(self):
        if self.worker:
            self.worker.stop()
            self.worker = None

    def connect(self):
        if self.worker:
            self.disconnect()
            self.status.set('Bağlantı kapatıldı')
            return
        if not self.port.get():
            messagebox.showerror('Port', 'Bir UART portu seçin')
            return
        self.running = False
        self.mode.set('uart')
        try:
            baud = int(self.baud.get())
            if not 1200 <= baud <= 4000000:
                raise ValueError('Baud 1200..4000000 olmalı')
            self.motor.locked = self.locked.get()
            self.worker = SerialWorker(self.motor, self.port.get(), baud)
            self.worker.start()
        except (ValueError, ImportError) as exc:
            messagebox.showerror('UART', str(exc))

    def reset(self):
        self.disconnect()
        self.running = False
        self.motor = Motor(self.motor.p)
        self.pi = DemoPI()
        self.history.clear()
        self.records.clear()
        self.started_wall = time.monotonic()
        self._record(self.motor.snapshot())
        self.status.set('Sıfırlandı · UART yeniden bağlanınca yeni session oluşturulur')

    def parameters(self):
        if self.worker or self.running:
            messagebox.showinfo('Parametreler', 'Parametre değiştirmeden önce duraklatın ve UART bağlantısını ayırın.')
            return
        win = tk.Toplevel(self.root)
        win.title('Motor parametreleri — ölçümle kalibre edin')
        win.configure(bg=BG)
        entries = {}
        for row, (name, value) in enumerate(asdict(self.motor.p).items()):
            ttk.Label(win, text=PARAM_LABELS[name]).grid(row=row, column=0, sticky='w', padx=14, pady=3)
            variable = tk.StringVar(value=str(value))
            entries[name] = variable
            ttk.Entry(win, textvariable=variable, width=22).grid(row=row, column=1, padx=14, pady=3)

        def parsed():
            values = {k: int(v.get()) if k in ('pole_pairs', 'encoder_cpr') else float(v.get()) for k, v in entries.items()}
            p = Parameters(**values)
            p.validate()
            return p

        def apply():
            try:
                self.motor = Motor(parsed())
                self.reset()
                win.destroy()
            except ValueError as exc:
                messagebox.showerror('Parametre', str(exc), parent=win)

        def save():
            try:
                content = json.dumps(asdict(parsed()), indent=2)
                path = filedialog.asksaveasfilename(defaultextension='.json', parent=win)
                if path:
                    Path(path).write_text(content, encoding='utf-8')
            except (ValueError, OSError) as exc:
                messagebox.showerror('Kaydet', str(exc), parent=win)

        def load():
            path = filedialog.askopenfilename(filetypes=[('Motor JSON', '*.json')], parent=win)
            if not path:
                return
            try:
                p = Parameters(**json.loads(Path(path).read_text(encoding='utf-8')))
                p.validate()
                for k, v in asdict(p).items():
                    entries[k].set(str(v))
            except (TypeError, ValueError, OSError) as exc:
                messagebox.showerror('Yükle', str(exc), parent=win)

        buttons = ttk.Frame(win)
        buttons.grid(row=len(entries), column=0, columnspan=2, pady=12)
        for label, command in [('JSON aç', load), ('JSON kaydet', save), ('Uygula / sıfırla', apply)]:
            ttk.Button(buttons, text=label, command=command).pack(side='left', padx=4)

    def _record(self, snapshot):
        row = dict(snapshot)
        row['target'] = self.pi.target if self.mode.get() == 'demo' else row.get('target', float('nan'))
        self.history.append(row)
        self.records.append(row)

    def export(self):
        path = filedialog.asksaveasfilename(defaultextension='.csv', filetypes=[('CSV', '*.csv')])
        if path and self.records:
            with open(path, 'w', newline='', encoding='utf-8-sig') as out:
                writer = csv.DictWriter(out, fieldnames=list(self.records[0]))
                writer.writeheader()
                writer.writerows(self.records)
            self.status.set(f'{len(self.records)} örnek kaydedildi')

    def tick(self):
        try:
            if self.running:
                target, kp, ki = self.target.get(), self.kp.get(), self.ki.get()
                if not all(math.isfinite(x) and x >= 0 for x in (target, kp, ki)):
                    raise ValueError('Hedef/Kp/Ki sonlu ve negatif olmayan sayılar olmalı')
                self.pi.target, self.pi.kp, self.pi.ki = target, kp, ki
                self.pi.reverse, self.pi.switched = self.reverse.get(), self.switched.get()
                self.motor.locked = self.locked.get()
                # 10 ms simulated per GUI frame. Physics never uses GUI wall dt.
                for _ in range(50):
                    self.motor.step(self.pi.command(self.motor), 0.0002)
                self._record(self.motor.snapshot())
                if self.motor.fault:
                    self.status.set(self.motor.fault)
            if self.worker:
                for _ in range(1024):
                    try:
                        self._record(self.worker.samples.get_nowait())
                    except queue.Empty:
                        break
                self.status.set(self.worker.error or self.worker.message)
            self.render()
        except Exception as exc:
            self.running = False
            self.status.set(f'Durduruldu: {exc}')
        self.root.after(30, self.tick)

    def render(self):
        s = self.history[-1]
        self.cards.delete('all')
        width = max(self.cards.winfo_width(), 900)
        values = [('AKIM', f"{s['measured']:.3f} A"), ('HIZ', f"{s['rpm']:.1f} RPM"),
                  ('HALL / SEKTÖR', f"{s['hall']:03b} / {s['sector']}"), ('ENCODER', str(s['encoder'])),
                  ('MODEL ZAMANI', f"{s['t']:.3f} s"), ('DUTY', f"%{s['duty']:.2f}")]
        for j, (label, value) in enumerate(values):
            x = j * width / 6
            self.cards.create_rectangle(x + 3, 3, x + width / 6 - 5, 88, fill=PANEL, outline='')
            self.cards.create_text(x + 14, 22, text=label, fill=MUTED, anchor='w', font=('Segoe UI', 9))
            self.cards.create_text(x + 14, 56, text=value, fill=TEXT, anchor='w', font=('Segoe UI', 15, 'bold'))
        for canvas, spec in zip(self.plots, PLOT_SPECS):
            self.plot(canvas, *spec)

    def plot(self, canvas, title, keys, labels):
        canvas.delete('all')
        w, h = max(canvas.winfo_width(), 100), max(canvas.winfo_height(), 100)
        canvas.create_text(14, 17, text=title, fill=TEXT, anchor='w', font=('Segoe UI', 10, 'bold'))
        canvas.create_text(w - 14, 17, text='Çift tık: detay', fill=MUTED, anchor='e', font=('Segoe UI', 8))
        rows = list(self.history)[-600:]
        values = [r[k] for r in rows for k in keys if math.isfinite(r[k])]
        lo, hi = min(values, default=0), max(values, default=1)
        span = max(hi - lo, 0.1)
        lo -= span * 0.15
        hi += span * 0.15
        x0, y0, x1, y1 = 56, 40, w - 12, h - 33
        for j in range(4):
            y = y0 + j * (y1 - y0) / 3
            canvas.create_line(x0, y, x1, y, fill='#2b3951')
            canvas.create_text(x0 - 6, y, text=f'{hi - j*(hi-lo)/3:.2f}', anchor='e', fill=MUTED, font=('Segoe UI', 8))
        t0 = rows[0]['t'] if rows else 0
        t1 = max(rows[-1]['t'] if rows else 1, t0 + 0.001)
        for j, key in enumerate(keys):
            points = []
            for row in rows:
                if math.isfinite(row[key]):
                    points.extend((x0 + (row['t'] - t0) / (t1 - t0) * (x1 - x0), y1 - (row[key] - lo) / (hi - lo) * (y1 - y0)))
            if len(points) >= 4:
                canvas.create_line(points, fill=COLORS[j], width=2)
            canvas.create_text(x0 + j * 135, h - 14, text=labels[j], fill=COLORS[j], anchor='w', font=('Segoe UI', 9))
        canvas.create_text(x1, h - 14, text=f'{t1:.3f} s', fill=MUTED, anchor='e', font=('Segoe UI', 8))

    def close(self):
        self.disconnect()
        for detail in tuple(self.detail_windows):
            if detail.window.winfo_exists():
                detail.window.destroy()
        self.root.destroy()


def launch(smoke=False):
    root = tk.Tk()
    app = App(root)
    if smoke:
        app.running = True
        def check():
            assert app.motor.t > 0
            assert app.cards.find_all()
            app.open_detail(0)
            app.root.update_idletasks()
            detail = app.detail_windows[0]
            detail.draw()
            assert detail.canvas.find_all()
            assert detail.visible_rows
            print('GUI smoke OK:', round(app.motor.t, 3), 's simulated')
            app.close()
        root.after(500, check)
    root.mainloop()
