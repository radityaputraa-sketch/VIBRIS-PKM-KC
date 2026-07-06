import sys
import os
import json
import csv
from collections import deque
from datetime import datetime

try:
    import serial
except ImportError:
    serial = None

from PyQt5.QtWidgets import (
    QApplication, QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QProgressBar
)
from PyQt5.QtCore import Qt, QTimer
import pyqtgraph as pg

# ===================== KONFIGURASI OPERASIONAL DETEKSI =====================
# PENTING: Ubah 'COM5' sesuai dengan port USB tempat ESP32 tercolok di laptop Anda
SERIAL_PORT = 'COM5'  
BAUD_RATE = 115200
BUFFER_LEN = 100
LOG_DIR = "logs"

# ===================== PALET WARNA INDUSTRI VIBRIS =====================
COL_BG_MAIN = "#2d3135"      
COL_PANEL = "#cfcfcf"        
COL_PANEL_DARK = "#1c1e22"   
COL_ACCENT = "#2a6f97"       
COL_TEXT_LIGHT = "#f2f2f2"
COL_TEXT_DARK = "#1c1c1c"
COL_OK = "#2e7d32"
COL_WARN = "#e08e00"
COL_BAD = "#c62828"

STATUS_COLOR = {"Normal": COL_OK, "Waspada": COL_WARN, "Bahaya": COL_BAD}


class Dashboard(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("VIBRIS Perangkat Pintar")
        
        # Mengunci dimensi mutlak panel LCD TFT 480 x 320
        self.setFixedSize(480, 320)
        self.setStyleSheet(f"background-color: {COL_BG_MAIN};")

        os.makedirs(LOG_DIR, exist_ok=True)

        # State Kendali Dinamis Alat
        self.selected_machine = "Belum Pilih Mesin"
        self.debug_mode = False
        self.recording = False
        self.csv_file = None
        self.csv_writer = None
        self.csv_filename = None

        # State Data Hasil Parsing JSON Transmitter ESP32
        self.latest = {"rms_v": 0, "rms_a": 0, "cur": 0, "temp": 0,
                        "rpm": 0, "d2": 0, "status": "UNKNOWN"}

        # Buffer Data Deret Waktu untuk Grafik Tren Sinyal Mentah (100 Titik)
        self.data_vib = deque([0] * BUFFER_LEN, maxlen=BUFFER_LEN)
        self.data_sound = deque([0] * BUFFER_LEN, maxlen=BUFFER_LEN)
        self.data_temp = deque([0] * BUFFER_LEN, maxlen=BUFFER_LEN)
        self.data_current = deque([0] * BUFFER_LEN, maxlen=BUFFER_LEN)

        # Inisialisasi Tata Letak Utama (Vertikal Master Grid)
        root = QVBoxLayout(self)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)

        # 1. BLOK ATAS: HEADER BAR
        root.addWidget(self._build_header())

        # 2. BLOK TENGAH: AREA KERJA UTAMA (Split Horizontal Konten & Info Kanan)
        body_layout = QHBoxLayout()
        body_layout.setSpacing(4)

        self.left_stack = self._build_left_stack()
        body_layout.addWidget(self.left_stack, 3)

        self.right_panel = self._build_right_info_panel()
        body_layout.addWidget(self.right_panel, 2)

        root.addLayout(body_layout)

        # 3. BLOK BAWAH: BOX MODE BOTTOM NAVIGATION BAR
        root.addWidget(self._build_bottom_navigation())

        # ===================== SISTEM TIMER & ENGINE KONEKSI =====================
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_all)
        self.timer.start(50)  # Interval 50ms menjamin pembacaan buffer serial sangat responsif

        self.clock_timer = QTimer()
        self.clock_timer.timeout.connect(self._update_clock)
        self.clock_timer.start(1000)
        self._update_clock()

        self.ser = None
        self.init_serial()

        # Buka Halaman Pertama Secara Default (Raw Reading)
        self.set_mode(0)

    def init_serial(self):
        if serial is not None:
            try:
                # Membuka koneksi COM dengan port laptop, timeout dibuat pendek agar UI responsif
                self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.05)
                print(f"[INFO] Sukses mendengarkan port serial: {SERIAL_PORT}")
            except Exception as e:
                print(f"[WARNING] Gagal membuka port {SERIAL_PORT}: {e}")
        self._set_connection_indicator(self.ser is not None)

    def _build_header(self):
        header = QFrame()
        header.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border-radius: 4px; max-height: 30px;")
        h = QHBoxLayout(header)
        h.setContentsMargins(6, 2, 6, 2)

        # KIRI ATAS: Dropdown Adaptif Pemilihan Alat Listrik AC Dinamis
        self.machine_combo = QComboBox()
        self.machine_combo.addItems([
            "Pilih Mesin AC...", 
            "Motor Induksi - Pompa Air", 
            "Blower Industri - UMKM", 
            "Kompresor - Produksi"
        ])
        self.machine_combo.setStyleSheet("background-color: #3a3f44; color: white; font-size: 10px; max-width: 150px;")
        self.machine_combo.currentIndexChanged.connect(self._machine_changed)
        h.addWidget(self.machine_combo)
        
        h.addStretch()

        # KANAN ATAS: Penunjuk Waktu dan Status Koneksi Inti Hardware VIBRIS
        self.clock_label = QLabel("--:--:--")
        self.clock_label.setStyleSheet(f"color: {COL_TEXT_LIGHT}; font-size: 9px; font-family: Arial;")
        h.addWidget(self.clock_label)

        h.addSpacing(6)

        self.conn_dot = QLabel("●")
        self.conn_dot.setStyleSheet(f"color: {COL_BAD}; font-size: 12px;")
        h.addWidget(self.conn_dot)
        
        self.conn_text = QLabel("OFFLINE")
        self.conn_text.setStyleSheet(f"color: {COL_TEXT_LIGHT}; font-size: 9px; font-weight: bold; font-family: Arial;")
        h.addWidget(self.conn_text)

        return header

    def _build_left_stack(self):
        stack = QStackedWidget()
        stack.setStyleSheet(f"background-color: {COL_PANEL}; border-radius: 4px;")
        
        # Menyesuaikan urutan halaman dengan benar: Raw -> Logs & Saves -> Processed -> Summary
        stack.addWidget(self._page_raw())         
        stack.addWidget(self._page_recording())   
        stack.addWidget(self._page_processed())   
        stack.addWidget(self._page_summary())     
        return stack

    def _page_raw(self):
        page = QWidget()
        grid = QGridLayout(page)
        grid.setContentsMargins(2, 2, 2, 2)
        grid.setSpacing(2)
        self.graphs = []
        titles = ["Vibration", "Sound", "Temp", "Current"]
        pens = ['r', 'y', '#ff8c00', 'c']
        for i in range(4):
            graph = pg.PlotWidget()
            graph.setBackground('k')
            graph.getAxis('left').setStyle(showValues=False)
            graph.getAxis('bottom').setStyle(showValues=False)
            graph.setTitle(titles[i], color=COL_TEXT_DARK, size="7pt")
            graph.showGrid(x=True, y=True, alpha=0.2)
            curve = graph.plot(pen=pg.mkPen(pens[i], width=1.5))
            self.graphs.append((graph, curve))
            grid.addWidget(graph, i // 2, i % 2)
        return page

    def _page_recording(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(2)

        self.debug_btn = QPushButton("Opsi Debug Logger: OFF")
        self.debug_btn.setStyleSheet("background-color: #54595e; color: white; font-size: 9px; min-height: 20px; font-family: Arial;")
        self.debug_btn.clicked.connect(self._toggle_debug_mode)
        layout.addWidget(self.debug_btn)

        self.rec_toggle_btn = QPushButton("MULAI RECORDING")
        self.rec_toggle_btn.setVisible(False)  
        self.rec_toggle_btn.setStyleSheet(f"background-color: {COL_ACCENT}; color: white; font-weight: bold; font-size: 10px; min-height: 25px; font-family: Arial;")
        self.rec_toggle_btn.clicked.connect(self.toggle_recording)
        layout.addWidget(self.rec_toggle_btn)

        self.rec_status_label = QLabel("● Sistem Standby")
        self.rec_status_label.setStyleSheet(f"font-size: 9px; font-weight: bold; color: {COL_BAD}; font-family: Arial;")
        layout.addWidget(self.rec_status_label)

        self.rec_list = QListWidget()
        self.rec_list.setStyleSheet("background-color: white; color: black; font-size: 8px;")
        layout.addWidget(self.rec_list)
        self._refresh_log_list()
        return page

    def _page_processed(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(4)

        self.proc_rpm_big = QLabel("RPM: --")
        self.proc_rpm_big.setStyleSheet(f"font-size: 20px; font-weight: bold; color: {COL_TEXT_DARK}; font-family: Arial;")
        self.proc_rpm_big.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.proc_rpm_big)

        self.proc_d2_big = QLabel("Mahalanobis D²: --")
        self.proc_d2_big.setStyleSheet(f"font-size: 16px; color: {COL_TEXT_DARK}; font-weight: bold; font-family: Arial;")
        self.proc_d2_big.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.proc_d2_big)

        lbl_bar = QLabel("Penyimpangan Sinyal Kedekatan Baseline:")
        lbl_bar.setStyleSheet("font-size: 8px; color: #333; font-weight: bold; font-family: Arial;")
        layout.addWidget(lbl_bar)

        self.d2_progress = QProgressBar()
        self.d2_progress.setMaximum(20) 
        self.d2_progress.setStyleSheet("""
            QProgressBar {
                border: 1px solid #777;
                background-color: #f0f0f0;
                border-radius: 3px;
                text-align: center;
                max-height: 15px;
                font-size: 8px;
                color: black;
            }
            QProgressBar::chunk {
                background-color: #2a6f97;
                width: 4px;
            }
        """)
        layout.addWidget(self.d2_progress)

        lbl_limit = QLabel("Ambang Batas Normal < 9.49  |  Bahaya > 13.28")
        lbl_limit.setStyleSheet("font-size: 8px; color: #555; font-family: Arial;")
        lbl_limit.setAlignment(Qt.AlignCenter)
        layout.addWidget(lbl_limit)
        return page

    def _page_summary(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setAlignment(Qt.AlignCenter)

        self.sum_status_big = QLabel("STATUS: --")
        self.sum_status_big.setStyleSheet("font-size: 24px; font-weight: bold; color: black; font-family: Arial;")
        self.sum_status_big.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.sum_status_big)

        self.sum_detail = QLabel("Silakan pilih target beban mesin AC pada menu kiri atas untuk mengaktifkan analisis kesehatan.")
        self.sum_detail.setStyleSheet(f"font-size: 9px; color: {COL_TEXT_DARK}; font-family: Arial;")
        self.sum_detail.setAlignment(Qt.AlignCenter)
        self.sum_detail.setWordWrap(True)
        layout.addWidget(self.sum_detail)
        return page

    def _build_right_info_panel(self):
        panel = QFrame()
        panel.setStyleSheet(f"background-color: {COL_PANEL}; border-radius: 4px; border: 1px solid #b5b5b5;")
        pl = QVBoxLayout(panel)
        pl.setContentsMargins(4, 4, 4, 4)
        pl.setSpacing(2)

        self.machine_side_title = QLabel("Target: Belum Pilih")
        self.machine_side_title.setStyleSheet(f"font-size: 10px; font-weight: bold; color: {COL_TEXT_DARK}; font-family: Arial;")
        pl.addWidget(self.machine_side_title)

        # Menggunakan Font Monospace untuk merapikan baris desimal data sensor digital
        self.values_label = QLabel("Vib  : 0.0000\nSnd  : 0.0\nAmp  : 0.0000\nTemp : 0.0\nRPM  : 0.0\nD2   : 0.00")
        self.values_label.setStyleSheet(f"font-size: 10px; color: {COL_TEXT_DARK}; font-family: 'Consolas', monospace;")
        pl.addWidget(self.values_label)

        self.content_desc = QLabel("Inisialisasi Sistem...")
        self.content_desc.setWordWrap(True)
        self.content_desc.setStyleSheet(f"font-size: 9px; color: #444; font-family: Arial;")
        pl.addWidget(self.content_desc)
        
        pl.addStretch()
        return panel

    def _build_bottom_navigation(self):
        nav_frame = QFrame()
        nav_frame.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border-radius: 4px; max-height: 45px;")
        nav = QHBoxLayout(nav_frame)
        nav.setContentsMargins(4, 4, 4, 4)
        nav.setSpacing(4)

        self.mode_buttons = []
        labels = ["Raw Reading", "Logs & Saves", "Processed", "Summary"]
        for i, label in enumerate(labels):
            btn = QPushButton(label)
            btn.setMinimumHeight(35)  
            btn.clicked.connect(lambda _, idx=i: self.set_mode(idx))
            nav.addWidget(btn)
            self.mode_buttons.append(btn)
        return nav_frame

    def set_mode(self, index):
        self.left_stack.setCurrentIndex(index)
        self._highlight_active_button(index)

        if index == 0:
            self._render_raw_desc()
        elif index == 1:
            self._refresh_log_list()
            self.content_desc.setText("Menggunakan auto-naming tersemat untuk menyimpan data komparasi baseline normal.")
        elif index == 2:
            self.content_desc.setText("Hasil inferensi komputasi estimasi rotasi RPM serta indeks penyimpangan grafik matriks.")
        elif index == 3:
            self.content_desc.setText("Status diagnosis akhir otomatis kondisi kesehatan komponen bearing atau beban motor AC.")

    def _highlight_active_button(self, active_index):
        for i, btn in enumerate(self.mode_buttons):
            if i == active_index:
                btn.setStyleSheet(f"background-color: {COL_ACCENT}; color: white; font-size: 9px; font-weight: bold; border: 1.5px solid white; border-radius: 3px; font-family: Arial;")
            else:
                btn.setStyleSheet(f"background-color: #54595e; color: {COL_TEXT_LIGHT}; font-size: 9px; border-radius: 3px; font-family: Arial;")

    def _machine_changed(self, index):
        if index == 0:
            self.selected_machine = "Belum Pilih Mesin"
        else:
            self.selected_machine = self.machine_combo.currentText()
        
        self.machine_side_title.setText(f"Target: {self.selected_machine}")
        self.set_mode(self.left_stack.currentIndex())

    def _toggle_debug_mode(self):
        self.debug_mode = not self.debug_mode
        if self.debug_mode:
            self.debug_btn.setText("Opsi Debug Logger: ON")
            self.debug_btn.setStyleSheet("background-color: #2e7d32; color: white; font-size: 9px; font-family: Arial;")
            self.rec_toggle_btn.setVisible(True)
        else:
            self.debug_btn.setText("Opsi Debug Logger: OFF")
            self.debug_btn.setStyleSheet("background-color: #54595e; color: white; font-size: 9px; font-family: Arial;")
            self.rec_toggle_btn.setVisible(False)
            if self.recording: self.toggle_recording()

    def _render_raw_desc(self):
        self.content_desc.setText(
            f"Keluaran Instan:\n"
            f"Vib: {self.latest['rms_v']:.4f}\n"
            f"Snd: {self.latest['rms_a']:.1f}\n"
            f"Amp: {self.latest['cur']:.4f}"
        )

    def _update_clock(self):
        self.clock_label.setText(datetime.now().strftime("%H:%M:%S"))

    def _set_connection_indicator(self, connected):
        if connected:
            self.conn_dot.setStyleSheet(f"color: {COL_OK}; font-size: 12px;")
            self.conn_text.setText("ONLINE")
        else:
            self.conn_dot.setStyleSheet(f"color: {COL_BAD}; font-size: 12px;")
            self.conn_text.setText("OFFLINE")

    # ==================== RECEIVER JALUR SERIAL UART LAPTOP ====================
    def update_all(self):
        # Coba buka ulang port pasif jika kabel terputus/sempat lepas
        if self.ser is None or not self.ser.is_open:
            self._set_connection_indicator(False)
            if serial is not None:
                try:
                    self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.01)
                except Exception:
                    return
            return

        try:
            if self.ser.in_waiting > 0:
                line = self.ser.readline().decode(errors='ignore').strip()
                
                # Pengaman parsing: data string UART wajib berupa objek JSON valid
                if not (line.startswith("{") and line.endswith("}")): 
                    return
                
                data = json.loads(line)
                self._set_connection_indicator(True)
            else:
                return
        except Exception:
            self._set_connection_indicator(False)
            return

        # Ambil data dari key dictionary (disesuaikan penuh dengan variabel pengiriman firmware Anda)
        self.latest = {
            "rms_v": data.get("rms_v", 0), "rms_a": data.get("rms_a", 0),
            "cur": data.get("cur", 0), "temp": data.get("temp", 0),
            "rpm": data.get("rpm", 0), "d2": data.get("d2", 0),
            "status": data.get("status", "UNKNOWN")
        }

        # Mengisi buffer ring untuk pyqtgraph real-time tren
        self.data_vib.append(self.latest["rms_v"])
        self.data_sound.append(self.latest["rms_a"])
        self.data_temp.append(self.latest["temp"])
        self.data_current.append(self.latest["cur"])

        buffers = [self.data_vib, self.data_sound, self.data_temp, self.data_current]
        for (graph, curve), buf in zip(self.graphs, buffers):
            curve.setData(list(buf))

        # Sinkronisasi parameter digital samping kanan
        self.values_label.setText(
            f"Vib  : {self.latest['rms_v']:.4f}\n"
            f"Snd  : {self.latest['rms_a']:.1f}\n"
            f"Amp  : {self.latest['cur']:.4f}\n"
            f"Temp : {self.latest['temp']:.1f}\n"
            f"RPM  : {self.latest['rpm']:.1f}\n"
            f"D2   : {self.latest['d2']:.2f}"
        )

        active = self.left_stack.currentIndex()
        if active == 0:
            self._render_raw_desc()
        elif active == 2:
            self.proc_rpm_big.setText(f"RPM: {self.latest['rpm']:.1f}")
            self.proc_d2_big.setText(f"Mahalanobis D²: {self.latest['d2']:.2f}")
            val_progress = int(clamp(self.latest['d2'], 0, 20))
            self.d2_progress.setValue(val_progress)
        elif active == 3:
            status = self.latest['status']
            color = STATUS_COLOR.get(status, COL_TEXT_DARK)
            self.sum_status_big.setText(f"STATUS: {status}")
            self.sum_status_big.setStyleSheet(f"font-size: 24px; font-weight: bold; color: {color}; font-family: Arial;")
            self.sum_detail.setText(f"Analisis: {self.selected_machine}\nNilai D2 terhitung: {self.latest['d2']:.2f}.\nKesimpulan Akhir: [{status}].")

        # Perekaman data otomatis jika tombol rekam aktif
        if self.recording and self.csv_writer:
            self.csv_writer.writerow([
                datetime.now().isoformat(), self.selected_machine,
                self.latest["rms_v"], self.latest["rms_a"], self.latest["cur"],
                self.latest["temp"], self.latest["rpm"], self.latest["d2"], self.latest["status"]
            ])
            self.csv_file.flush()

    def toggle_recording(self):
        if not self.recording:
            # Menggunakan penamaan otomatis dengan stempel waktu adaptif agar operator UMKM tidak perlu mengetik
            clean_name = self.selected_machine.replace(" ", "").replace("-", "")
            filename = os.path.join(LOG_DIR, f"{clean_name}_{datetime.now().strftime('%m%d_%H%M%S')}.csv")
            
            self.csv_file = open(filename, 'w', newline='')
            self.csv_writer = csv.writer(self.csv_file)
            self.csv_writer.writerow(['timestamp', 'machine', 'rms_v', 'rms_a', 'cur', 'temp', 'rpm', 'd2', 'status'])
            self.csv_filename = filename
            self.recording = True
            
            self.rec_status_label.setText(f"● LOG DATA -> {os.path.basename(filename)}")
            self.rec_status_label.setStyleSheet(f"font-size: 9px; font-weight: bold; color: {COL_OK}; font-family: Arial;")
            self.rec_toggle_btn.setText("BERHENTI RECORDING")
        else:
            self.recording = False
            if self.csv_file: self.csv_file.close()
            self.rec_status_label.setText(f"● Berkas Disimpan Otomatis")
            self.rec_status_label.setStyleSheet(f"font-size: 9px; font-weight: bold; color: {COL_BAD}; font-family: Arial;")
            self.rec_toggle_btn.setText("MULAI RECORDING")
            self._refresh_log_list()

    def _refresh_log_list(self):
        if not hasattr(self, "rec_list"): return
        self.rec_list.clear()
        if os.path.isdir(LOG_DIR):
            for f in sorted(os.listdir(LOG_DIR), reverse=True):
                if f.endswith(".csv"): self.rec_list.addItem(f)

    def closeEvent(self, event):
        if self.csv_file: self.csv_file.close()
        event.accept()

def clamp(n, minn, maxn):
    return max(min(n, maxn), minn)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = Dashboard()
    win.show()
    sys.exit(app.exec_())
