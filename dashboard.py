import sys
import os
import csv
from datetime import datetime
from collections import deque

# Import Serial & Threading untuk Integrasi Komunikasi Data Hardware ESP32
try:
    import serial
    import threading
except ImportError:
    serial = None

from PyQt5.QtWidgets import (
    QApplication, QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QMessageBox,
    QSlider, QDialog
)
from PyQt5.QtCore import QTimer, Qt
import pyqtgraph as pg

# ===================== KONFIGURASI OPERASIONAL =====================
# Menyesuaikan dengan port USB-Enhanced-SERIAL CH343 laptop kamu yang terdeteksi
SERIAL_PORT = 'COM6'  
BAUD_RATE = 115200
LOG_DIR = "logs"
if not os.path.exists(LOG_DIR):
    os.makedirs(LOG_DIR)

# ===================== PALET WARNA INDUSTRI VIBRIS =====================
COL_BG_MAIN = "#2d3135"      
COL_PANEL_DARK = "#1c1e22"   
COL_ACCENT = "#2a6f97"       
COL_TEXT_LIGHT = "#f2f2f2"
COL_OK = "#2e7d32"
COL_WARN = "#e08e00"
COL_BAD = "#c62828"

class LogDetailDialog(QDialog):
    """
    Panel baru (jendela terpisah) yang muncul begitu sebuah file rekaman (.csv) di klik
    pada daftar Logs & Saves. Menampilkan grafik hasil deteksi (Vibration, Sound, Current,
    Temp) beserta status diagnosa ringkas dari seluruh sesi rekaman tersebut, dan dilengkapi
    kontrol Play/Pause/Speed/Slider mandiri untuk memutar ulang datanya secara animasi.
    """
    def __init__(self, filepath, filename, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"Hasil Deteksi - {filename}")
        self.resize(950, 620)
        self.setStyleSheet(f"background-color: {COL_BG_MAIN}; color: {COL_TEXT_LIGHT}; font-family: Arial;")

        # Memuat seluruh isi berkas CSV rekaman ke dalam list data
        self.times, self.v_vals, self.a_vals, self.cur_vals, self.t_vals = self._load_csv(filepath)

        # State animasi playback milik panel ini sendiri (terpisah dari dashboard utama)
        self.play_index = 0
        self.play_speed = 1.0
        self.timer = QTimer(self)
        self.timer.timeout.connect(self._step)

        root = QVBoxLayout(self)
        root.setContentsMargins(8, 8, 8, 8)
        root.setSpacing(6)

        title_lbl = QLabel(f"HASIL DETEKSI REKAMAN: {filename}")
        title_lbl.setStyleSheet("font-size: 11px; font-weight: bold; color: #ffffff;")
        root.addWidget(title_lbl)

        # ===== GRID 4 GRAFIK (GAYA SAMA DENGAN TAB RAW READING) =====
        grid = QGridLayout()
        grid.setSpacing(4)
        pg.setConfigOptions(antialias=True)

        self.graph_v = pg.PlotWidget(title="Vibration (G)")
        self.graph_v.setBackground(COL_PANEL_DARK)
        self.graph_v.showGrid(x=True, y=True, alpha=0.3)
        self.curve_v = self.graph_v.plot(pen=pg.mkPen('#ff4d4d', width=1.5))
        grid.addWidget(self.graph_v, 0, 0)

        self.graph_a = pg.PlotWidget(title="Sound (dB)")
        self.graph_a.setBackground(COL_PANEL_DARK)
        self.graph_a.showGrid(x=True, y=True, alpha=0.3)
        self.curve_a = self.graph_a.plot(pen=pg.mkPen('#4da6ff', width=1.5))
        grid.addWidget(self.graph_a, 0, 1)

        self.graph_cur = pg.PlotWidget(title="Current (A)")
        self.graph_cur.setBackground(COL_PANEL_DARK)
        self.graph_cur.showGrid(x=True, y=True, alpha=0.3)
        self.curve_cur = self.graph_cur.plot(pen=pg.mkPen('#ffeb3b', width=1.5))
        grid.addWidget(self.graph_cur, 1, 0)

        self.graph_temp = pg.PlotWidget(title="Temp (°C)")
        self.graph_temp.setBackground(COL_PANEL_DARK)
        self.graph_temp.showGrid(x=True, y=True, alpha=0.3)
        self.curve_temp = self.graph_temp.plot(pen=pg.mkPen('#e040fb', width=1.5))
        grid.addWidget(self.graph_temp, 1, 1)

        root.addLayout(grid, 1)

        # ===== PANEL STATUS DIAGNOSA HASIL REKAMAN =====
        self.box_diagnosis = QFrame()
        self.box_diagnosis.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px solid #444; border-radius: 6px;")
        box_lay = QVBoxLayout(self.box_diagnosis)
        box_lay.setContentsMargins(6, 4, 6, 4)

        self.lbl_diag_status = QLabel("STATUS: -")
        self.lbl_diag_status.setAlignment(Qt.AlignCenter)
        self.lbl_diag_status.setStyleSheet("font-size: 12px; font-weight: bold;")
        box_lay.addWidget(self.lbl_diag_status)

        self.lbl_diag_desc = QLabel("")
        self.lbl_diag_desc.setWordWrap(True)
        self.lbl_diag_desc.setStyleSheet("font-size: 9px; color: #ccc;")
        box_lay.addWidget(self.lbl_diag_desc)

        self.lbl_diag_peak = QLabel("")
        self.lbl_diag_peak.setStyleSheet("font-size: 9px; color: #aaa;")
        box_lay.addWidget(self.lbl_diag_peak)

        root.addWidget(self.box_diagnosis)

        # ===== KONTROL PLAYBACK MANDIRI (PLAY/PAUSE, SPEED, SLIDER) =====
        ctrl = QHBoxLayout()
        ctrl.setSpacing(4)

        self.btn_play_pause = QPushButton("▶ PLAY")
        self.speed_combo = QComboBox()
        self.speed_combo.addItems(["0.5x", "1x", "2x", "4x"])
        self.speed_combo.setCurrentIndex(1)
        self.btn_close = QPushButton("TUTUP PANEL")

        self.btn_play_pause.setStyleSheet("background-color: #cfcfcf; color: #000000; font-weight: bold; font-size: 9px; height: 26px; border-radius: 4px;")
        self.btn_close.setStyleSheet(f"background-color: {COL_BAD}; color: #ffffff; font-weight: bold; font-size: 9px; height: 26px; border-radius: 4px;")
        self.speed_combo.setStyleSheet(f"background-color: {COL_PANEL_DARK}; color: white; font-size: 9px; padding: 2px;")

        self.btn_play_pause.clicked.connect(self._toggle_play)
        self.speed_combo.currentIndexChanged.connect(self._change_speed)
        self.btn_close.clicked.connect(self.close)

        self.slider = QSlider(Qt.Horizontal)
        self.slider.setMinimum(0)
        self.slider.setMaximum(max(0, len(self.times) - 1))
        self.slider.sliderMoved.connect(self._seek)

        self.lbl_pos = QLabel(f"{len(self.times)} / {len(self.times)}" if self.times else "0 / 0")
        self.lbl_pos.setStyleSheet("font-size: 8px; color: #aaa;")

        ctrl.addWidget(self.btn_play_pause, 2)
        ctrl.addWidget(self.speed_combo, 1)
        ctrl.addWidget(self.slider, 5)
        ctrl.addWidget(self.lbl_pos, 1)
        ctrl.addWidget(self.btn_close, 2)
        root.addLayout(ctrl)

        # Tampilan awal: seluruh grafik rekaman langsung terlihat penuh (overview),
        # sekaligus status diagnosa hasil deteksi dari sesi rekaman ini.
        self._render_full_overview()
        self._render_diagnosis_summary()

    def _load_csv(self, filepath):
        """Membaca seluruh isi berkas CSV rekaman menjadi list per kolom sensor."""
        times, v_vals, a_vals, cur_vals, t_vals = [], [], [], [], []
        try:
            with open(filepath, 'r') as f:
                reader = csv.reader(f)
                next(reader)  # lewati baris header
                for i, row in enumerate(reader):
                    times.append(i)
                    v_vals.append(float(row[2]))
                    a_vals.append(float(row[3]))
                    cur_vals.append(float(row[4]))
                    t_vals.append(float(row[5]))
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Gagal membaca berkas rekaman: {e}")
        return times, v_vals, a_vals, cur_vals, t_vals

    def _render_full_overview(self):
        """Menampilkan seluruh isi rekaman sekaligus di grafik (tampilan hasil deteksi penuh)."""
        self.curve_v.setData(self.times, self.v_vals)
        self.curve_a.setData(self.times, self.a_vals)
        self.curve_cur.setData(self.times, self.cur_vals)
        self.curve_temp.setData(self.times, self.t_vals)
        if self.times:
            self.slider.blockSignals(True)
            self.slider.setValue(len(self.times) - 1)
            self.slider.blockSignals(False)

    def _render_diagnosis_summary(self):
        """Menghitung nilai puncak (peak) selama sesi rekaman dan menentukan status diagnosa akhir."""
        if not self.v_vals:
            self.lbl_diag_status.setText("STATUS: DATA KOSONG")
            self.lbl_diag_desc.setText("Berkas rekaman tidak memiliki data yang dapat dianalisis.")
            return

        peak_v = max(self.v_vals)
        peak_a = max(self.a_vals)
        peak_cur = max(self.cur_vals)
        peak_temp = max(self.t_vals)

        # Menggunakan ambang batas yang sama seperti diagnosa live pada dashboard utama
        if peak_v > 0.25 or peak_temp > 50.0:
            status, col = "BAHAYA (CRITICAL)", COL_BAD
            desc = "Terdeteksi puncak getaran dan/atau suhu yang melampaui ambang batas kritis selama sesi rekaman ini."
        elif peak_v > 0.18 or peak_temp > 42.0:
            status, col = "WASPADA (WARNING)", COL_WARN
            desc = "Terdeteksi indikasi awal ketidakseimbangan massa atau degradasi mekanis selama sesi rekaman ini."
        else:
            status, col = "NORMAL", COL_OK
            desc = "Seluruh parameter selama sesi rekaman ini berada di bawah ambang batas deviasi kritis."

        self.lbl_diag_status.setText(f"STATUS HASIL DETEKSI: {status}")
        self.lbl_diag_status.setStyleSheet(f"font-size: 12px; font-weight: bold; color: {col};")
        self.box_diagnosis.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 2px solid {col}; border-radius: 6px;")
        self.lbl_diag_desc.setText(desc)
        self.lbl_diag_peak.setText(
            f"Nilai puncak selama rekaman -> Vib: {peak_v:.2f} G | Snd: {peak_a:.1f} dB | "
            f"Cur: {peak_cur:.2f} A | Tmp: {peak_temp:.1f} °C"
        )

    def _render_frame(self, idx):
        """Menggambar 1 frame animasi (jendela 50 titik terakhir) untuk efek 'muter ulang'."""
        start = max(0, idx - 49)
        t = self.times[start:idx + 1]
        v = self.v_vals[start:idx + 1]
        a = self.a_vals[start:idx + 1]
        c = self.cur_vals[start:idx + 1]
        tp = self.t_vals[start:idx + 1]

        self.curve_v.setData(t, v)
        self.curve_a.setData(t, a)
        self.curve_cur.setData(t, c)
        self.curve_temp.setData(t, tp)

        self.slider.blockSignals(True)
        self.slider.setValue(idx)
        self.slider.blockSignals(False)
        self.lbl_pos.setText(f"{idx + 1} / {len(self.times)}")

    def _step(self):
        if self.play_index >= len(self.times):
            self.timer.stop()
            self.btn_play_pause.setText("▶ PLAY")
            return
        self._render_frame(self.play_index)
        self.play_index += 1

    def _toggle_play(self):
        if not self.times:
            return
        if self.timer.isActive():
            self.timer.stop()
            self.btn_play_pause.setText("▶ PLAY")
        else:
            # Jika sudah di akhir data, otomatis ulang dari awal
            if self.play_index >= len(self.times):
                self.play_index = 0
            interval_ms = max(20, int(200 / self.play_speed))
            self.timer.start(interval_ms)
            self.btn_play_pause.setText("⏸ PAUSE")

    def _change_speed(self, combo_idx):
        mapping = {0: 0.5, 1: 1.0, 2: 2.0, 3: 4.0}
        self.play_speed = mapping.get(combo_idx, 1.0)
        if self.timer.isActive():
            self.timer.start(max(20, int(200 / self.play_speed)))

    def _seek(self, value):
        self.play_index = value
        self._render_frame(value)

    def closeEvent(self, event):
        # Pastikan timer animasi berhenti total saat panel ditutup, agar tidak berjalan di latar belakang
        self.timer.stop()
        super().closeEvent(event)


class Dashboard(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("HMI | Rotating Machinery Detection System")
        
        # ===== PENGATURAN DEDICATED FULLSCREEN UNTUK LAYAR TFT RASPBERRY PI =====
        self.setWindowFlags(Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint)
        self.setStyleSheet(f"background-color: {COL_BG_MAIN}; color: {COL_TEXT_LIGHT}; font-family: Arial;")

        # Data Buffer Sinkronisasi Plot Grafik (Panjang data 50 point antrean)
        self.data_len = 50
        self.time_buffer = deque(maxlen=self.data_len)
        self.v_buffer = deque(maxlen=self.data_len)
        self.a_buffer = deque(maxlen=self.data_len)
        self.cur_buffer = deque(maxlen=self.data_len)
        self.temp_buffer = deque(maxlen=self.data_len)
        self.tick = 0

        # Variabel Penampung Nilai Sensor Terkini (Awalnya kosong sebelum ada data serial masuk)
        self.current_v = None
        self.current_a = None
        self.current_cur = None
        self.current_temp = None

        # Status Operasional Recording & Serial Hardware
        self.recording = False
        self.csv_file = None
        self.csv_writer = None
        self.ser = None
        self.serial_connected = False

        # Root Layout Utama Vertikal
        root = QVBoxLayout(self)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)

        # ===== HEADER BAR =====
        header = QHBoxLayout()
        title_lbl = QLabel("VIBRIS DETEKSI DINI MESIN ROTASI")
        title_lbl.setStyleSheet("font-size: 10px; font-weight: bold; color: #ffffff;")
        self.time_lbl = QLabel("--:--:--")
        self.time_lbl.setStyleSheet("font-size: 10px; font-weight: bold; color: #00e676;")
        self.time_lbl.setAlignment(Qt.AlignRight)
        header.addWidget(title_lbl)
        header.addWidget(self.time_lbl)
        root.addLayout(header)

        # ===== STACKED PAGES (KONTEN UTAMA DI TENGAH) =====
        self.stack = QStackedWidget()
        self.stack.addWidget(self._page_raw())       # Index 0
        self.stack.addWidget(self._page_recording()) # Index 1
        self.stack.addWidget(self._page_processed()) # Index 2
        self.stack.addWidget(self._page_summary())   # Index 3
        root.addWidget(self.stack, 1)

        # ===== NAVIGASI TOMBOL UTAMA DI SEBELAH BAWAH (WARNA HURUF HITAM CONTRAS) =====
        nav_bottom = QHBoxLayout()
        nav_bottom.setSpacing(4)
        
        self.btn_raw = QPushButton("RAW READING")
        self.btn_rec = QPushButton("LOGS SAVES")
        self.btn_proc = QPushButton("PROCESSED")
        self.btn_sum = QPushButton("SUMMARY")

        self.menu_buttons = [self.btn_raw, self.btn_rec, self.btn_proc, self.btn_sum]
        for i, btn in enumerate(self.menu_buttons):
            btn.setFixedHeight(38)  # Tinggi ergonomis ramah sentuhan jari pada layar kecil
            btn.setStyleSheet(self._menu_style(False))
            btn.clicked.connect(lambda checked, idx=i: self._change_page(idx))
            nav_bottom.addWidget(btn)
        
        root.addLayout(nav_bottom)

        # ===== MENGAKTIFKAN LOGIKA UTARA SENSOR SERIAL =====
        self._init_serial_connection()

        # ===== TIMER UTAMA REFRESH GUI (Per 200 milidetik) =====
        self.main_timer = QTimer(self)
        self.main_timer.timeout.connect(self._update_gui)
        self.main_timer.start(200) 

        # Set Halaman Awal & Muat Berkas Rekaman (.csv) bawaan ketua tim
        self._change_page(0)
        self._refresh_log_list()
        self.showFullScreen()

    def _menu_style(self, active):
        # Seluruh tulisan teks pada tombol menu dipastikan tetap berwarna hitam pekat kontras tinggi
        if active:
            return f"background-color: {COL_ACCENT}; color: #000000; font-size: 9px; font-weight: bold; border: 1px solid white; border-radius: 4px;"
        return f"background-color: #cfcfcf; color: #000000; font-size: 9px; font-weight: bold; border: 1px solid #444; border-radius: 4px;"

    def _change_page(self, idx):
        self.stack.setCurrentIndex(idx)
        for i, btn in enumerate(self.menu_buttons):
            btn.setStyleSheet(self._menu_style(i == idx))

    # ===================== HALAMAN 1: RAW READING (4 KOTAK GRAFIK INDIVIDU) =====================
    def _page_raw(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.setSpacing(2)

        top_bar = QHBoxLayout()
        top_bar.addWidget(QLabel("Target Mesin:"))
        self.machine_combo = QComboBox()
        self.machine_combo.addItems([
            "- Pilih Mesin Target -",
            "Blower Industri UMKM", 
            "Motor Induksi Pompa Air", 
            "Kompresor Production"
        ])
        self.machine_combo.setStyleSheet(f"background-color: {COL_PANEL_DARK}; color: white; font-size: 9px; padding: 2px;")
        top_bar.addWidget(self.machine_combo, 1)
        layout.addLayout(top_bar)

        main_raw = QHBoxLayout()
        
        # Grid layout untuk membagi porsi layar menjadi 4 bagian grafik sub-plot dedicated
        layout_grafik_grid = QGridLayout()
        layout_grafik_grid.setSpacing(2)
        
        pg.setConfigOptions(antialias=True)
        
        # 1. Grafik Getaran (Vibration)
        self.graph_v = pg.PlotWidget(title="Vibration (G)")
        self.graph_v.setBackground(COL_PANEL_DARK)
        self.graph_v.showGrid(x=True, y=True, alpha=0.3)
        self.curve_v = self.graph_v.plot(pen=pg.mkPen('#ff4d4d', width=1.5))
        layout_grafik_grid.addWidget(self.graph_v, 0, 0)
        
        # 2. Grafik Suara (Sound)
        self.graph_a = pg.PlotWidget(title="Sound (dB)")
        self.graph_a.setBackground(COL_PANEL_DARK)
        self.graph_a.showGrid(x=True, y=True, alpha=0.3)
        self.curve_a = self.graph_a.plot(pen=pg.mkPen('#4da6ff', width=1.5))
        layout_grafik_grid.addWidget(self.graph_a, 0, 1)
        
        # 3. Grafik Arus Listrik (Current)
        self.graph_cur = pg.PlotWidget(title="Current (A)")
        self.graph_cur.setBackground(COL_PANEL_DARK)
        self.graph_cur.showGrid(x=True, y=True, alpha=0.3)
        self.curve_cur = self.graph_cur.plot(pen=pg.mkPen('#ffeb3b', width=1.5))
        layout_grafik_grid.addWidget(self.graph_cur, 1, 0)
        
        # 4. Grafik Suhu (Temperature)
        self.graph_temp = pg.PlotWidget(title="Temp (°C)")
        self.graph_temp.setBackground(COL_PANEL_DARK)
        self.graph_temp.showGrid(x=True, y=True, alpha=0.3)
        self.curve_temp = self.graph_temp.plot(pen=pg.mkPen('#e040fb', width=1.5))
        layout_grafik_grid.addWidget(self.graph_temp, 1, 1)
        
        main_raw.addLayout(layout_grafik_grid, 7)

        # Panel Kanan Bagian Nilai Numerik Polosan
        panel_kanan = QVBoxLayout()
        panel_kanan.setSpacing(2)
        
        frame_status = QFrame()
        frame_status.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border-radius: 4px;")
        fs_lay = QVBoxLayout(frame_status)
        fs_lay.setContentsMargins(4, 4, 4, 4)
        
        self.lbl_sys_status = QLabel("● STANDBY")
        self.lbl_sys_status.setStyleSheet(f"font-size: 9px; font-weight: bold; color: #888888;")
        fs_lay.addWidget(self.lbl_sys_status)
        
        self.lbl_val_v = QLabel("Vib: -- G")
        self.lbl_val_a = QLabel("Snd: -- dB")
        self.lbl_val_cur = QLabel("Cur: -- A")
        self.lbl_val_temp = QLabel("Tmp: -- °C")

        for lbl in [self.lbl_val_v, self.lbl_val_a, self.lbl_val_cur, self.lbl_val_temp]:
            lbl.setStyleSheet("font-size: 8px; color: #aaa;")
            fs_lay.addWidget(lbl)

        panel_kanan.addWidget(frame_status)
        main_raw.addLayout(panel_kanan, 3)

        layout.addLayout(main_raw)
        return page

    # ===================== HALAMAN 2: LOGS & SAVES (KONTROL REKAM DATA) =====================
    def _page_recording(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.setSpacing(4)

        rec_ctrl = QHBoxLayout()
        self.btn_toggle_rec = QPushButton("MULAI RECORDING")
        self.btn_toggle_rec.setStyleSheet("background-color: #cfcfcf; color: #000000; font-weight: bold; font-size: 9px; height: 24px; border-radius: 4px;")
        self.btn_toggle_rec.clicked.connect(self._toggle_recording)
        
        self.lbl_rec_status = QLabel("● Device Standby")
        self.lbl_rec_status.setStyleSheet("font-size: 8px; color: #aaa;")
        
        rec_ctrl.addWidget(self.btn_toggle_rec, 2)
        rec_ctrl.addWidget(self.lbl_rec_status, 1)
        layout.addLayout(rec_ctrl)

        lbl_daftar = QLabel("Daftar Rekaman Mesin (.CSV):")
        lbl_daftar.setStyleSheet("font-size: 8px;")
        layout.addWidget(lbl_daftar)
        
        self.log_list = QListWidget()
        self.log_list.setStyleSheet(f"background-color: {COL_PANEL_DARK}; color: white; font-size: 9px;")
        # Klik langsung pada file rekaman -> otomatis membuka panel baru berisi grafik hasil deteksi
        self.log_list.itemClicked.connect(self._open_log_detail)
        layout.addWidget(self.log_list)

        btn_lay = QHBoxLayout()
        self.btn_watch_log = QPushButton("Buka Panel Deteksi")
        self.btn_delete_log = QPushButton("Hapus Rekaman")
        
        self.btn_watch_log.setStyleSheet("background-color: #cfcfcf; color: #000000; font-size: 9px; height: 20px; font-weight: bold; border-radius: 3px;")
        self.btn_delete_log.setStyleSheet("background-color: #cfcfcf; color: #000000; font-size: 9px; height: 20px; font-weight: bold; border-radius: 3px;")
        
        self.btn_watch_log.clicked.connect(self._open_log_detail_from_button)
        self.btn_delete_log.clicked.connect(self._delete_selected_log)
        
        btn_lay.addWidget(self.btn_watch_log)
        btn_lay.addWidget(self.btn_delete_log)
        layout.addLayout(btn_lay)

        lbl_hint = QLabel("* Klik salah satu file rekaman di atas untuk membuka panel hasil deteksi\n"
                           "  (grafik Vibration/Sound/Current/Temp beserta status diagnosanya).")
        lbl_hint.setStyleSheet("font-size: 8px; color: #888; font-style: italic; margin-top: 4px;")
        lbl_hint.setWordWrap(True)
        layout.addWidget(lbl_hint)

        return page

    # ===================== HALAMAN 3: PROCESSED READING (KOMPARASI BASELINE) =====================
    def _page_processed(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(4, 4, 4, 4)
        
        grid = QGridLayout()
        grid.setSpacing(4)

        lbl_header_param = QLabel("PARAMETER")
        lbl_header_real = QLabel("REAL-TIME")
        lbl_header_base = QLabel("BASELINE (NORMAL)")
        
        lbl_header_param.setStyleSheet("font-weight: bold; font-size: 9px;")
        lbl_header_real.setStyleSheet("font-weight: bold; font-size: 9px;")
        lbl_header_base.setStyleSheet("font-weight: bold; font-size: 9px;")

        grid.addWidget(lbl_header_param, 0, 0)
        grid.addWidget(lbl_header_real, 0, 1)
        grid.addWidget(lbl_header_base, 0, 2)

        self.proc_v = [QLabel("Vibration RMS"), QLabel("-"), QLabel("0.15 G")]
        self.proc_a = [QLabel("Sound Level"), QLabel("-"), QLabel("45.0 dB")]
        self.proc_c = [QLabel("Current Motor"), QLabel("-"), QLabel("1.20 A")]
        self.proc_t = [QLabel("Suhu Bearing"), QLabel("-"), QLabel("36.5 °C")]

        for row, labels in enumerate([self.proc_v, self.proc_a, self.proc_c, self.proc_t], start=1):
            for col, lbl in enumerate(labels):
                lbl.setStyleSheet(f"font-size: 9px; background-color: {COL_PANEL_DARK}; padding: 4px; border-radius: 2px; color: #aaa;")
                grid.addWidget(lbl, row, col)

        layout.addLayout(grid)
        
        self.lbl_proc_note = QLabel("* Alat dalam kondisi standby. Menunggu aliran data UART...")
        self.lbl_proc_note.setStyleSheet("font-size: 8px; color: #aaa; font-style: italic;")
        layout.addWidget(self.lbl_proc_note)
        return page

    # ===================== HALAMAN 4: DIAGNOSIS SUMMARY (KESIMPULAN KONDISI) =====================
    def _page_summary(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(6)

        lbl_summary_title = QLabel("KESIMPULAN DIAGNOSA KONDISI MESIN:")
        lbl_summary_title.setStyleSheet("font-weight: bold; font-size: 10px;")
        layout.addWidget(lbl_summary_title)

        self.box_diagnosis = QFrame()
        self.box_diagnosis.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px solid #444; border-radius: 6px;")
        box_lay = QVBoxLayout(self.box_diagnosis)
        
        self.lbl_diag_status = QLabel("STATUS MESIN: STANDBY")
        self.lbl_diag_status.setStyleSheet("font-size: 12px; font-weight: bold; color: #888888;")
        self.lbl_diag_status.setAlignment(Qt.AlignCenter)
        box_lay.addWidget(self.lbl_diag_status)

        self.lbl_diag_desc = QLabel("Belum ada data deteksi yang diproses. Silakan pilih target mesin dan hubungkan kabel hardware.")
        self.lbl_diag_desc.setStyleSheet("font-size: 9px; color: #888;")
        self.lbl_diag_desc.setWordWrap(True)
        box_lay.addWidget(self.lbl_diag_desc)

        layout.addWidget(self.box_diagnosis)
        return page

    # ===================== LOGIKA THREAD BACKEND BACA REAL-TIME DATA SERIAL =====================
    def _init_serial_connection(self):
        if serial is not None:
            # Membuka thread background baru khusus membaca UART secara non-blocking
            t = threading.Thread(target=self._read_serial_worker, daemon=True)
            t.start()
        else:
            print("Library pyserial tidak terinstal atau tidak terdeteksi pada Python interpreter.")

    def _read_serial_worker(self):
        while True:
            try:
                if self.ser is None or not self.ser.is_open:
                    self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
                    self.serial_connected = True
                
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    parts = line.split(',')
                    # Memeriksa keutuhan paket data (Wajib memiliki 4 elemen sensor)
                    if len(parts) >= 4:
                        self.current_v = float(parts[0])
                        self.current_a = float(parts[1])
                        self.current_cur = float(parts[2])
                        self.current_temp = float(parts[3])
                        
                        # Injeksi data masuk ke dalam antrean buffer internal grafik
                        self.tick += 1
                        self.time_buffer.append(self.tick)
                        self.v_buffer.append(self.current_v)
                        self.a_buffer.append(self.current_a)
                        self.cur_buffer.append(self.current_cur)
                        self.temp_buffer.append(self.current_temp)

                        # Menyimpan data riil ke dalam berkas CSV jika perekaman aktif
                        if self.recording and self.csv_writer:
                            self.csv_writer.writerow([
                                datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                                self.machine_combo.currentText(),
                                self.current_v, self.current_a, self.current_cur, self.current_temp
                            ])
            except Exception as e:
                self.serial_connected = False
                self.ser = None
                # Reset data ke kondisi kosong jika kabel USB terputus
                self.current_v = self.current_a = self.current_cur = self.current_temp = None

    # ===================== EVALUASI DIAGNOSA MESIN (DIPAKAI TAB SUMMARY) =====================
    def _evaluate_diagnosis(self, v, temp):
        # Evaluasi Kritis Kondisi Mesin (Diagnosa Otomatis di Tab Summary)
        if v > 0.25 or temp > 50.0:
            self.lbl_diag_status.setText("STATUS MESIN: BAHAYA (CRITICAL)")
            self.lbl_diag_status.setStyleSheet(f"font-size: 12px; font-weight: bold; color: {COL_BAD};")
            self.box_diagnosis.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 2px solid {COL_BAD}; border-radius: 6px;")
            self.lbl_diag_desc.setText("Terjadi anomali gesekan parah atau ketiadaan lubrikasi bearing! Segera matikan mesin produksi.")
            status_txt, status_col = "● DEVIASI BAHAYA", COL_BAD
        elif v > 0.18 or temp > 42.0:
            self.lbl_diag_status.setText("STATUS MESIN: WASPADA (WARNING)")
            self.lbl_diag_status.setStyleSheet(f"font-size: 12px; font-weight: bold; color: {COL_WARN};")
            self.box_diagnosis.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 2px solid {COL_WARN}; border-radius: 6px;")
            self.lbl_diag_desc.setText("Indikasi awal ketidakseimbangan massa atau degradasi mekanis bearing terdeteksi.")
            status_txt, status_col = "● STATUS WASPADA", COL_WARN
        else:
            self.lbl_diag_status.setText("STATUS MESIN: NORMAL")
            self.lbl_diag_status.setStyleSheet(f"font-size: 12px; font-weight: bold; color: {COL_OK};")
            self.box_diagnosis.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 2px solid {COL_OK}; border-radius: 6px;")
            self.lbl_diag_desc.setText("Seluruh parameter berjalan di bawah ambang batas deviasi krisis karsa cipta. Mesin aman digunakan.")
            status_txt, status_col = "● SYSTEM ONLINE", COL_OK

        self.lbl_sys_status.setText(status_txt)
        self.lbl_sys_status.setStyleSheet(f"font-size: 9px; font-weight: bold; color: {status_col};")

    # ===================== LOGIKA EMBEDDED KOMPARASI & REFRESH GUI (MODE LIVE) =====================
    def _update_gui(self):
        # Update teks Jam digital di bagian baris atas
        now = datetime.now().strftime("%H:%M:%S")
        self.time_lbl.setText(now)

        # Logika eksekusi jika data sensor valid masuk dari port serial COM6
        if self.current_v is not None:
            # Refresh pergerakan 4 gelombang garis grafik secara simultan
            self.curve_v.setData(list(self.time_buffer), list(self.v_buffer))
            self.curve_a.setData(list(self.time_buffer), list(self.a_buffer))
            self.curve_cur.setData(list(self.time_buffer), list(self.cur_buffer))
            self.curve_temp.setData(list(self.time_buffer), list(self.temp_buffer))

            # Sinkronisasi teks angka di tab Raw Reading
            self.lbl_val_v.setText(f"Vib: {self.current_v:.2f} G")
            self.lbl_val_a.setText(f"Snd: {self.current_a:.1f} dB")
            self.lbl_val_cur.setText(f"Cur: {self.current_cur:.2f} A")
            self.lbl_val_temp.setText(f"Tmp: {self.current_temp:.1f} °C")

            # Sinkronisasi teks perbandingan di tab Processed Reading
            self.proc_v[1].setText(f"{self.current_v:.2f} G")
            self.proc_a[1].setText(f"{self.current_a:.1f} dB")
            self.proc_c[1].setText(f"{self.current_cur:.2f} A")
            self.proc_t[1].setText(f"{self.current_temp:.1f} °C")
            self.lbl_proc_note.setText("* Data diolah melalui Edge Computing terkomparasi Statistical Self-Baseline.")

            # Evaluasi diagnosa otomatis berdasarkan data live
            self._evaluate_diagnosis(self.current_v, self.current_temp)

    def _toggle_recording(self):
        if self.machine_combo.currentIndex() == 0:
            QMessageBox.warning(self, "Perhatian", "Silakan pilih Target Mesin terlebih dahulu!")
            return
            
        if not self.recording:
            filename = os.path.join(LOG_DIR, f"log_{datetime.now().strftime('%d%m%Y_%H%M%S')}.csv")
            try:
                self.csv_file = open(filename, 'w', newline='')
                self.csv_writer = csv.writer(self.csv_file)
                self.csv_writer.writerow(['timestamp', 'machine_type', 'rms_v', 'rms_a', 'current', 'temp'])
                
                self.recording = True
                self.btn_toggle_rec.setText("BERHENTI RECORDING")
                self.btn_toggle_rec.setStyleSheet(f"background-color: {COL_BAD}; color: #ffffff; font-weight: bold; font-size: 9px; height: 24px;")
                self.lbl_rec_status.setText(f"● MENULIS -> {os.path.basename(filename)}")
                self.lbl_rec_status.setStyleSheet(f"font-size: 8px; color: {COL_WARN};")
            except Exception as e:
                print(f"Gagal membuat file log: {e}")
        else:
            self.recording = False
            if self.csv_file:
                self.csv_file.close()
                self.csv_file = None
            self.btn_toggle_rec.setText("MULAI RECORDING")
            self.btn_toggle_rec.setStyleSheet("background-color: #cfcfcf; color: #000000; font-weight: bold; font-size: 9px; height: 24px;")
            self.lbl_rec_status.setText("● Berkas Tersimpan")
            self.lbl_rec_status.setStyleSheet(f"font-size: 8px; color: {COL_OK};")
            self._refresh_log_list()

    def _refresh_log_list(self):
        self.log_list.clear()
        if os.path.isdir(LOG_DIR):
            for file in sorted(os.listdir(LOG_DIR), reverse=True):
                if file.endswith(".csv"):
                    self.log_list.addItem(file)

    def _open_log_detail(self, item):
        """Dipanggil saat sebuah file rekaman di klik pada daftar -> membuka panel baru (popup)
        yang menampilkan grafik hasil deteksi (Vibration/Sound/Current/Temp) beserta diagnosanya."""
        if item is None:
            return
        filename = item.text()
        filepath = os.path.join(LOG_DIR, filename)
        dialog = LogDetailDialog(filepath, filename, parent=self)
        dialog.exec_()

    def _open_log_detail_from_button(self):
        """Tombol cadangan 'Buka Panel Deteksi' - memakai file yang sedang terpilih di daftar."""
        current_item = self.log_list.currentItem()
        if not current_item:
            QMessageBox.warning(self, "Perhatian", "Pilih file rekaman (.csv) terlebih dahulu!")
            return
        self._open_log_detail(current_item)

    def _delete_selected_log(self):
        current_item = self.log_list.currentItem()
        if not current_item:
            return
            
        filename = current_item.text()
        filepath = os.path.join(LOG_DIR, filename)
        
        reply = QMessageBox.question(
            self, 'Konfirmasi Hapus', f"Apakah Anda yakin ingin menghapus berkas rekaman {filename}?",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No
        )
        
        if reply == QMessageBox.Yes:
            try:
                if os.path.exists(filepath):
                    os.remove(filepath)
                self._refresh_log_list()
                self.lbl_rec_status.setText("● BERKAS DIHAPUS")
                self.lbl_rec_status.setStyleSheet(f"font-size: 8px; color: {COL_BAD};")
            except Exception as e:
                print(f"Gagal menghapus file: {e}")

    def keyPressEvent(self, event):
        # Tekan tombol 'ESC' pada keyboard eksternal untuk menutup paksa aplikasi full-screen
        if event.key() == Qt.Key_Escape:
            if self.csv_file:
                self.csv_file.close()
            self.close()

if __name__ == '__main__':
    app = QApplication(sys.argv)
    db = Dashboard()
    db.show()
    sys.exit(app.exec_())
