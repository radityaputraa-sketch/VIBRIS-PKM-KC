import sys, os, csv
from datetime import datetime
from PyQt5.QtWidgets import *
from PyQt5.QtCore import QTimer, Qt
import pyqtgraph as pg

class Dashboard(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Deteksi Dini Mesin Rotasi")
        
        # ===== PENGATURAN LAYAR DEDICATED TFT RASPBERRY PI =====
        # Menghilangkan window frame (close, maximize, minimize) & membuat window selalu di depan
        self.setWindowFlags(Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint)
        self.setStyleSheet("background-color: #1a1c1e; color: white;")

        root = QVBoxLayout(self)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)

        # ===== HEADER =====
        header = QLabel("HMI | Rotating Machinery Detection System")
        header.setStyleSheet("font-size: 11px; font-weight: bold; color: #ffffff;")
        header.setAlignment(Qt.AlignCenter)
        root.addWidget(header)

        # ===== STACKED PAGES =====
        self.stack = QStackedWidget()
        self.stack.addWidget(self._page_raw())       # index 0
        self.stack.addWidget(self._page_recording()) # index 1
        self.stack.addWidget(self._page_processed()) # index 2
        self.stack.addWidget(self._page_summary())   # index 3
        root.addWidget(self.stack, 1)

        # ===== NAVIGATION BOTTOM =====
        nav = QHBoxLayout()
        labels = ["Raw\nPlot", "Log\nRecord", "Processed\nReading", "Summary"]
        self.buttons = []
        for i, label in enumerate(labels):
            btn = QPushButton(label)
            btn.setMinimumHeight(45)
            btn.setStyleSheet("background-color:#3a3f44; color:white; font-size:10px;")
            btn.clicked.connect(lambda _, idx=i: self.set_mode(idx))
            nav.addWidget(btn)
            self.buttons.append(btn)
        nav_frame = QFrame()
        nav_frame.setLayout(nav)
        nav_frame.setStyleSheet("background-color:#111315; border-radius:4px;")
        root.addWidget(nav_frame)

        # ===== INITIALIZE DATA BUFFERS (KOSONG TOTAL - TANPA GARIS AWAL) =====
        # Inisialisasi dengan array kosong [] agar tidak ada garis saat start
        self.data_vib = []
        self.data_sound = []
        self.data_temp = []
        self.data_current = []
        
        # Array baseline untuk perbandingan di processed reading page
        self.baseline_vib = []

        # Panjang maksimum buffer data (misal 50 titik)
        self.max_data_points = 50

        # ===== TIMER REAL-TIME =====
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_all)
        self.timer.start(200) # Interval update 200ms

        # ===== RECORDING VARS =====
        self.recording = False
        self.csv_file = None
        self.csv_writer = None
        self.csv_filename = None

        # ===== INITIALIZE DISPLAY =====
        self.set_mode(0)
        self.showFullScreen() 

    # ===== PAGE 0: RAW PLOT =====
    def _page_raw(self):
        page = QWidget()
        grid = QGridLayout(page)
        grid.setContentsMargins(2, 2, 2, 2)
        grid.setSpacing(2)
        
        self.raw_curves = []
        titles = ["Vibration (m/s²)", "Sound (dB)", "Temp (°C)", "Current (A)"]
        pens = ['r', 'y', '#ff8c00', 'c']
        
        for i in range(4):
            graph = pg.PlotWidget()
            graph.setBackground('k')
            graph.setTitle(titles[i], color="w", size="6pt")
            graph.showGrid(x=True, y=True, alpha=0.2)
            
            # Curve objek, diinisialisasi tanpa data awal
            curve = graph.plot(pen=pg.mkPen(pens[i], width=1.5))
            self.raw_curves.append(curve)
            grid.addWidget(graph, i//2, i%2)
        return page

    # ===== PAGE 1: RECORDING & LOGS (WITH DELETE FEATURE) =====
    def _page_recording(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(3)

        top_bar = QHBoxLayout()
        rec_title = QLabel("Recording & Saves")
        rec_title.setStyleSheet("font-size: 11px; font-weight: bold;")
        self.rec_status_label = QLabel("● IDLE")
        self.rec_status_label.setStyleSheet("font-size: 10px; color: gray;")
        self.rec_status_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        top_bar.addWidget(rec_title)
        top_bar.addWidget(self.rec_status_label)
        layout.addLayout(top_bar)

        self.rec_toggle_btn = QPushButton("START RECORD")
        self.rec_toggle_btn.setMinimumHeight(32)
        self.rec_toggle_btn.setStyleSheet("background-color: #007acc; color: white; font-weight: bold; font-size: 10px; border-radius: 4px;")
        self.rec_toggle_btn.clicked.connect(self.toggle_recording)
        layout.addWidget(self.rec_toggle_btn)

        self.rec_list = QListWidget()
        self.rec_list.setStyleSheet("font-size: 9px; background-color: #25282a; border-radius: 4px; padding: 2px;")
        layout.addWidget(self.rec_list)

        self.delete_btn = QPushButton("DELETE SELECTED LOG")
        self.delete_btn.setMinimumHeight(28)
        self.delete_btn.setStyleSheet("background-color: #5c1d1d; color: #ff4d4d; font-weight: bold; font-size: 9px; border: 1px solid #ff4d4d; border-radius: 4px;")
        self.delete_btn.clicked.connect(self.delete_selected_log)
        layout.addWidget(self.delete_btn)

        self.rec_list.itemDoubleClicked.connect(lambda _: self.open_selected_log())
        self._refresh_log_list()
        return page

    # ===== PAGE 2: PROCESSED READING =====
    def _page_processed(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.setSpacing(4)

        title = QLabel("Processed Reading (Real-time vs Baseline)")
        title.setStyleSheet("font-size: 10px; font-weight: bold; color: #00ffcc;")
        layout.addWidget(title)

        self.processed_graph = pg.PlotWidget()
        self.processed_graph.setBackground('k')
        self.processed_graph.showGrid(x=True, y=True, alpha=0.2)
        self.processed_graph.addLegend(size=(60, 30), offset=(10, 10))
        
        self.curve_processed_current = self.processed_graph.plot(pen=pg.mkPen('r', width=1.5), name="Current")
        self.curve_processed_baseline = self.processed_graph.plot(pen=pg.mkPen('g', width=1.2, style=Qt.DashLine), name="Baseline")
        
        layout.addWidget(self.processed_graph)
        return page

    # ===== PAGE 3: SUMMARY =====
    def _page_summary(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)
        
        title = QLabel("Statistical Analysis Summary")
        title.setStyleSheet("font-size:11px; font-weight:bold; color: #ffcc00;")
        layout.addWidget(title)
        
        grid = QGridLayout()
        grid.setSpacing(4)
        self.sum_labels = {}
        params = ["Vibration", "Sound", "Temp", "Current"]
        
        for i, p in enumerate(params):
            lbl = QLabel(f"{p}:\nWaiting Data...")
            lbl.setAlignment(Qt.AlignCenter)
            lbl.setStyleSheet("font-size:10px; font-weight:bold; background-color:#2a2d30; border: 1px solid #444; border-radius:4px; padding:6px;")
            grid.addWidget(lbl, i//2, i%2)
            self.sum_labels[p] = lbl
        layout.addLayout(grid)
            
        self.diagnosis_box = QLabel("DIAGNOSIS: WAITING FOR INTEGRATION...")
        self.diagnosis_box.setMinimumHeight(40)
        self.diagnosis_box.setAlignment(Qt.AlignCenter)
        self.diagnosis_box.setStyleSheet("font-size:11px; font-weight:bold; background-color:#222; border-radius:4px; border: 1px solid #ffcc00; color: #ffcc00;")
        layout.addWidget(self.diagnosis_box)
        
        return page

    # ===== NAVIGATION =====
    def set_mode(self, idx):
        self.stack.setCurrentIndex(idx)
        for i, btn in enumerate(self.buttons):
            if i == idx:
                btn.setStyleSheet("background-color:#007acc; color:white; font-weight:bold; font-size:10px; border-radius: 4px;")
            else:
                btn.setStyleSheet("background-color:#3a3f44; color:white; font-size:10px; border-radius: 4px;")

    # ===== TEMPAT INTEGRASI HARDWARE DATA SENSOR RIIL =====
    def update_all(self):
        """
        [TEMPAT INTEGRASI]
        Silakan masukkan fungsi pembacaan data sensor kamu di sini.
        Fungsi ini harus mengembalikan nilai float/int riil dari sensor, atau None jika gagal.
        """
        
        # SEMENTARA DISET NONE (KOSONG TOTAL) - SILAKAN GANTI DENGAN VARIABEL REAL SENSOR
        val_vib = None
        val_sound = None
        val_temp = None
        val_current = None

        # Update buffer array: Tambahkan data baru hanya jika datanya ada (tidak None)
        has_data = False
        if val_vib is not None:
            self.data_vib.append(val_vib)
            if len(self.data_vib) > self.max_data_points: self.data_vib.pop(0)
            self.raw_curves[0].setData(self.data_vib)
            has_data = True
            
        if val_sound is not None:
            self.data_sound.append(val_sound)
            if len(self.data_sound) > self.max_data_points: self.data_sound.pop(0)
            self.raw_curves[1].setData(self.data_sound)
            has_data = True

        if val_temp is not None:
            self.data_temp.append(val_temp)
            if len(self.data_temp) > self.max_data_points: self.data_temp.pop(0)
            self.raw_curves[2].setData(self.data_temp)
            has_data = True

        if val_current is not None:
            self.data_current.append(val_current)
            if len(self.data_current) > self.max_data_points: self.data_current.pop(0)
            self.raw_curves[3].setData(self.data_current)
            has_data = True

        # Jangan update grafik jika tidak ada data sensor (kosong total saat start)
        if not has_data:
            return

        # 2. Update kurva grafik di halaman 'Processed Reading'
        if self.stack.currentIndex() == 2 and self.data_vib:
            self.curve_processed_current.setData(self.data_vib)
            # update baseline jika ada data
            if self.baseline_vib:
                self.curve_processed_baseline.setData(self.baseline_vib)

        # 3. Logika thresholding dan update halaman 'Summary'
        if self.stack.currentIndex() == 3:
            # SILAKAN MASUKKAN AMBANG BATAS (THRESHOLD) RIIL PROTOTIPE MAKIN KE DEPAN DI SINI
            pass

        # 4. Proses Penyimpanan Data Log secara otomatis jika tombol Record aktif
        if self.recording and self.csv_writer and has_data:
            self.csv_writer.writerow([
                datetime.now().isoformat(),
                val_vib if val_vib is not None else 0,
                val_sound if val_sound is not None else 0,
                val_temp if val_temp is not None else 0,
                val_current if val_current is not None else 0,
                0, # Kolom untuk nilai RPM (jika ada)
                0  # Kolom untuk statistical parameter tambahan (misal d2)
            ])
            self.csv_file.flush()

    # ===== RECORDING & FILE FUNCTIONS =====
    def toggle_recording(self):
        if not self.recording:
            os.makedirs("logs", exist_ok=True)
            filename = os.path.join("logs", f"rec_{datetime.now().strftime('%m%d_%H%M%S')}.csv")
            self.csv_file = open(filename, 'w', newline='')
            self.csv_writer = csv.writer(self.csv_file)
            self.csv_writer.writerow(['timestamp','vibration','sound','temp','current','rpm','d2'])
            self.csv_filename = filename
            self.recording = True
            
            self.rec_status_label.setText("● LOGGING...")
            self.rec_status_label.setStyleSheet("color:#ffcc00; font-weight:bold; font-size:10px;")
            self.rec_toggle_btn.setText("STOP RECORD")
            self.rec_toggle_btn.setStyleSheet("background-color:#ff0000; color:white; font-weight:bold; font-size:10px; border-radius: 4px;")
        else:
            self.recording = False
            if self.csv_file:
                self.csv_file.close()
                
            self.rec_status_label.setText("● SAVED")
            self.rec_status_label.setStyleSheet("color:#4dff5b; font-weight:bold; font-size:10px;")
            self.rec_toggle_btn.setText("START RECORD")
            self.rec_toggle_btn.setStyleSheet("background-color:#007acc; color:white; font-weight:bold; font-size:10px; border-radius: 4px;")
            self._refresh_log_list()

    def _refresh_log_list(self):
        self.rec_list.clear()
        if os.path.isdir("logs"):
            for f in sorted(os.listdir("logs"), reverse=True):
                if f.endswith(".csv"):
                    self.rec_list.addItem(f)

    def open_selected_log(self):
        item = self.rec_list.currentItem()
        if item:
            filename = os.path.join("logs", item.text())
            print("Opening:", filename)
            
            try:
                import pandas as pd
                import matplotlib.pyplot as plt
                
                df = pd.read_csv(filename)
                for col in ["vibration","sound","temp","current"]:
                    if col in df.columns:
                        df[col] = pd.to_numeric(df[col], errors="coerce")
                df = df.dropna()
                df.plot(x="timestamp", y=["vibration","sound","temp","current"])
                plt.show()
            except ImportError:
                print("Error: Matplotlib atau Pandas belum terinstal di Raspberry Pi.")

    def delete_selected_log(self):
        current_item = self.rec_list.currentItem()
        if not current_item:
            self.rec_status_label.setText("⚠️ SELECT A FILE FIRST")
            self.rec_status_label.setStyleSheet("color: #ff4d4d; font-size: 9px;")
            return
            
        filename_to_delete = current_item.text()
        filepath = os.path.join("logs", filename_to_delete)
        
        reply = QMessageBox.question(
            self, 'Konfirmasi Hapus', 
            f"Hapus file {filename_to_delete}?",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No
        )
        
        if reply == QMessageBox.Yes:
            try:
                if os.path.exists(filepath):
                    os.remove(filepath)
                self.rec_status_label.setText("● DELETED")
                self.rec_status_label.setStyleSheet("color: red; font-size: 10px;")
                self._refresh_log_list()
            except Exception as e:
                print(f"Gagal menghapus file: {e}")
                self.rec_status_label.setText("● ERROR DELETE")
                self.rec_status_label.setStyleSheet("color: #ff4d4d; font-size: 10px;")

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Escape:
            self.close()

    def closeEvent(self, event):
        if self.csv_file:
            self.csv_file.close()
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = Dashboard()
    win.show()
    sys.exit(app.exec_())
