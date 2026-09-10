"""Bounded GUI/serial worker. Wall-clock timeout freezes lockstep simulation."""
import queue
import threading
import time
from .protocol import Endpoint, Parser, COMMAND


class SerialWorker(threading.Thread):
    def __init__(self, motor, port, baud):
        super().__init__(daemon=True)
        self.endpoint = Endpoint(motor)
        self.port_name, self.baud = port, baud
        self.stop_event = threading.Event()
        self.samples = queue.Queue(maxsize=1024)
        self.message = 'Bağlanıyor'
        self.error = ''

    def run(self):
        import serial
        port = None
        try:
            port = serial.Serial(port=None, baudrate=self.baud, timeout=0.02, write_timeout=0.25)
            port.dtr = port.rts = False
            port.port = self.port_name
            port.open()
            self.message = 'STM32 protokol v3 komutu bekleniyor'
            parser = Parser()
            last_rx = last_ready = time.monotonic()
            port.write(self.endpoint.reply())
            while not self.stop_event.is_set():
                data = port.read(min(max(port.in_waiting, 1), 2048))
                errors_before = parser.errors
                frames = parser.feed(data)
                self.endpoint.errors += parser.errors - errors_before
                for kind, payload in frames:
                    if kind != COMMAND:
                        continue
                    previous = self.endpoint.sequence
                    result = self.endpoint.accept(payload)
                    if result is not None:
                        port.write(result)
                        # Replays don't renew watchdog or advance the plot.
                        if previous != self.endpoint.sequence:
                            last_rx = time.monotonic()
                            self.message = 'STM32 PI · senkron model adımları'
                            sample = self.endpoint.motor.snapshot()
                            sample['target'] = self.endpoint.target_current
                            try:
                                self.samples.put_nowait(sample)
                            except queue.Full:
                                self.samples.get_nowait()
                                self.samples.put_nowait(sample)
                now = time.monotonic()
                if self.endpoint.sequence is not None and now - last_rx >= 0.5:
                    self.endpoint.timed_out = True
                    self.message = 'Komut zaman aşımı · simülasyon zamanı durdu'
                if now - last_ready >= 1 and (self.endpoint.sequence is None or self.endpoint.timed_out):
                    port.write(self.endpoint.reply())
                    last_ready = now
        except Exception as exc:
            self.error = str(exc)
            self.message = 'Bağlantı durdu'
        finally:
            if port is not None:
                port.close()

    def stop(self):
        self.stop_event.set()
        self.join(timeout=2)
