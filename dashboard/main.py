# main.py
# Kerangka utama aplikasi. Merakit semua view (viewraw, viewrecording,
# viewprocessed, viewsummary, viewmachinemanager) ke dalam satu window
# 480x320 tetap. Tanggung jawab file ini HANYA: build header+nav+debug
# panel, routing antar halaman (QStackedWidget), baca serial port lalu
# broadcast data ke tiap view lewat update_data()/update_live(), dan
# jembatan buka Machine Manager overlay. TIDAK boleh berisi logic
# spesifik satu halaman (misal detail grafik, CSV) — itu tanggung jawab
# file view masing-masing.

import sys, json
from PyQt5.QtWidgets import QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QStackedWidget, QFrame
from PyQt5.QtCore import Qt, QTimer
from datetime import datetime

try:
    import serial
except ImportError:
    serial = None

import config
import styles
from viewraw import ViewRaw
from viewrecording import ViewRecording
from viewprocessed import ViewProcessed
from viewsummary import ViewSummary
from viewmachinemanager import ViewMachineManager


class Dashboard(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("VIBRIS HMI")
        self.setFixedSize(480, 320)
        self.setStyleSheet(f"background-color:{config.COL_BG_MAIN};")

        self.current_machine = dict(config.DEFAULT_MACHINE)
        self.machines = [dict(config.DEFAULT_MACHINE)]
        self.debug_on = False
        self.latest = {"rms_v": 0, "rms_a": 0, "cur": 0, "temp": 0, "rpm": 0, "d2": 0, "status": "UNKNOWN"}

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        root.addWidget(self._build_header())

        # Stack root: [main_content_area] with overlay for machine manager & debug drawn on top
        self.body_stack = QStackedWidget()
        self.content_stack = QStackedWidget()

        self.view_raw = ViewRaw()
        self.view_recording = ViewRecording(open_processed_cb=self.open_processed)
        self.view_processed = ViewProcessed()
        self.view_summary = ViewSummary()
        self.view_machine_manager = ViewMachineManager(
            machines=self.machines,
            on_select=self.set_active_machine,
            on_close=self.close_machine_manager,
        )

        for w in (self.view_raw, self.view_recording, self.view_processed, self.view_summary):
            self.content_stack.addWidget(w)

        self.body_stack.addWidget(self.content_stack)   # index 0: normal
        self.body_stack.addWidget(self.view_machine_manager)  # index 1: overlay

        root.addWidget(self.body_stack, 1)

        self.debug_panel = self._build_debug_panel()
        self.debug_panel.setVisible(False)
        root.addWidget(self.debug_panel)

        self.nav_frame, self.nav_buttons = self._build_bottom_nav()
        root.addWidget(self.nav_frame)

        self.timer = QTimer()
        self.timer.timeout.connect(self.poll_serial)
        self.timer.start(50)

        self.clock_timer = QTimer()
        self.clock_timer.timeout.connect(self._tick_clock)
        self.clock_timer.start(1000)
        self._tick_clock()

        self.ser = None
        self._init_serial()
        self.set_mode(0)

    # ---------------- HEADER ----------------
    def _build_header(self):
        header = QFrame()
        header.setFixedHeight(24)
        header.setStyleSheet(styles.header_style())
        h = QHBoxLayout(header)
        h.setContentsMargins(4, 0, 4, 0)

        self.machine_btn = QPushButton(self.current_machine["label"])
        self.machine_btn.setStyleSheet(f"background-color:{config.COL_PANEL_DARK}; color:white; font-size:9px; padding:2px 6px;")
        self.machine_btn.clicked.connect(self.open_machine_manager)
        h.addWidget(self.machine_btn)
        h.addStretch()

        self.debug_btn = QPushButton("DEBUG")
        self.debug_btn.setStyleSheet(f"background-color:{config.COL_PANEL_DARK}; color:white; font-size:8px; padding:2px 6px;")
        self.debug_btn.clicked.connect(self.toggle_debug)
        h.addWidget(self.debug_btn)

        self.conn_dot = QLabel("●")
        self.conn_dot.setStyleSheet(f"color:{config.COL_BAD}; font-size:11px;")
        h.addWidget(self.conn_dot)

        self.clock_label = QLabel("--:--:--")
        self.clock_label.setStyleSheet(f"color:{config.COL_TEXT_DARK}; font-size:9px; font-family:Consolas;")
        h.addWidget(self.clock_label)
        return header

    def _build_debug_panel(self):
        panel = QFrame()
        panel.setFixedHeight(60)
        panel.setStyleSheet(styles.debug_panel_style())
        lay = QVBoxLayout(panel)
        lay.setContentsMargins(4, 2, 4, 2)
        self.debug_text = QLabel("[DEBUG] standby...")
        self.debug_text.setWordWrap(True)
        self.debug_text.setStyleSheet("color:#38f27a; font-family:Consolas; font-size:8px;")
        lay.addWidget(self.debug_text)
        return panel

    def _build_bottom_nav(self):
        frame = QFrame()
        frame.setFixedHeight(30)
        frame.setStyleSheet(styles.bottom_nav_style())
        nav = QHBoxLayout(frame)
        nav.setContentsMargins(3, 3, 3, 3)
        nav.setSpacing(3)
        labels = ["Raw", "Recording", "Processed", "Summary"]
        buttons = []
        for i, label in enumerate(labels):
            btn = QPushButton(label)
            btn.setStyleSheet(styles.nav_btn_style(i == 0))
            btn.clicked.connect(lambda _, idx=i: self.set_mode(idx))
            nav.addWidget(btn)
            buttons.append(btn)
        return frame, buttons

    def set_mode(self, index):
        self.content_stack.setCurrentIndex(index)
        for i, btn in enumerate(self.nav_buttons):
            btn.setStyleSheet(styles.nav_btn_style(i == index))

    def open_processed(self, filepath):
        self.view_processed.load_file(filepath)
        self.set_mode(2)

    # ------------- MACHINE MANAGER -------------
    def open_machine_manager(self):
        self.body_stack.setCurrentIndex(1)

    def close_machine_manager(self):
        self.body_stack.setCurrentIndex(0)

    def set_active_machine(self, machine):
        self.current_machine = machine
        self.machine_btn.setText(machine["label"])
        self.close_machine_manager()

    # ------------- DEBUG -------------
    def toggle_debug(self):
        self.debug_on = not self.debug_on
        self.debug_panel.setVisible(self.debug_on)

    # ------------- SERIAL -------------
    def _init_serial(self):
        if serial is not None:
            try:
                self.ser = serial.Serial(config.SERIAL_PORT, config.BAUD_RATE, timeout=0.05)
            except Exception as e:
                self._log_debug(f"WARN: gagal buka {config.SERIAL_PORT}: {e}")
        self._set_conn(self.ser is not None)

    def poll_serial(self):
        if self.ser is None or not self.ser.is_open:
            self._set_conn(False)
            return
        try:
            if self.ser.in_waiting > 0:
                line = self.ser.readline().decode(errors='ignore').strip()
                if not (line.startswith("{") and line.endswith("}")):
                    self._log_debug(line)
                    return
                data = json.loads(line)
                self._set_conn(True)
            else:
                return
        except Exception as e:
            self._set_conn(False)
            self._log_debug(f"ERROR serial: {e}")
            return

        self.latest = {
            "rms_v": data.get("rms_v", 0), "rms_a": data.get("rms_a", 0),
            "cur": data.get("cur", 0), "temp": data.get("temp", 0),
            "rpm": data.get("rpm", 0), "d2": data.get("d2", 0),
            "status": data.get("status", "UNKNOWN"),
        }
        self._log_debug(json.dumps(self.latest))
        self.view_raw.update_data(self.latest)
        self.view_recording.update_live(self.latest)
        self.view_summary.update_data(self.latest)

    def _set_conn(self, connected):
        self.conn_dot.setStyleSheet(f"color:{config.COL_OK if connected else config.COL_BAD}; font-size:11px;")

    def _log_debug(self, text):
        if hasattr(self, "debug_text"):
            self.debug_text.setText(f"[{datetime.now().strftime('%H:%M:%S')}] {text}")

    def _tick_clock(self):
        self.clock_label.setText(datetime.now().strftime("%H:%M:%S"))


if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = Dashboard()
    win.show()
    sys.exit(app.exec_())