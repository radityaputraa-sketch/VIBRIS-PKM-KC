import sys, random, os, csv
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
        header = QLabel("HMI | Electric Fan")
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

        # ===== DATA & TIMER =====
        self.data = [0]*50
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_all)
        self.timer.start(200)

        # ===== RECORDING VARS =====
        self.recording = False
        self.csv_file = None
        self.csv_writer = None
        self.csv_filename = None

        # ===== INITIALIZE DISPLAY =====
        self.set_mode(0)
        self.showFullScreen() # Memaksa aplikasi memenuhi seluruh resolusi layar TFT

    # ===== PAGES =====
    def _page_raw(self):
        page = QWidget()
        grid = QGridLayout(page)
        self.graphs = []
        titles = ["Vibration (m/s²)", "Sound", "Temp (°C)", "Current (A)"]
        pens = ['r','y','#ff8c00','c']
        for i in range(4):
            graph = pg.PlotWidget()
            graph.setBackground('k')
            graph.setTitle(titles[i], color="w", size="8pt")
            graph.showGrid(x=True,y=True,alpha=0.2)
            curve = graph.plot(pen=pg.mkPen(pens[i], width=1.5))
            self.graphs.append((graph, curve))
            grid.addWidget(graph, i//2, i%2)
        return page

    def _page_recording(self):
        page = QWidget()
        layout = QVBoxLayout(page)

        rec_title = QLabel("Recording & Saves")
        layout.addWidget(rec_title)

        self.rec_status_label = QLabel("● IDLE")
        layout.addWidget(self.rec_status_label)

        self.rec_toggle_btn = QPushButton("START RECORD")
        self.rec_toggle_btn.clicked.connect(self.toggle_recording)
        layout.addWidget(self.rec_toggle_btn)

        self.rec_list = QListWidget()
        layout.addWidget(self.rec_list)

        # >>> koneksi event double-click <<<
        self.rec_list.itemDoubleClicked.connect(lambda _: self.open_selected_log())

        self._refresh_log_list()
        return page

    def _page_processed(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.addWidget(QLabel("Processed Reading"))
        return page

    def _page_summary(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        
        title = QLabel("Summary")
        title.setStyleSheet("font-size:12px; font-weight:bold;")
        layout.addWidget(title)
        
        self.sum_labels = {}
        params = ["Vibration", "Sound", "Temp", "Current"]
        for p in params:
            lbl = QLabel(f"{p}: --")
            lbl.setStyleSheet("font-size:11px; font-weight:bold; background-color:#333; color:white; padding:4px;")
            layout.addWidget(lbl)
            self.sum_labels[p] = lbl
            
        return page

    # ===== NAVIGATION =====
    def set_mode(self, idx):
        self.stack.setCurrentIndex(idx)
        for i, btn in enumerate(self.buttons):
            if i == idx:
                btn.setStyleSheet("background-color:#007acc; color:white; font-weight:bold;")
            else:
                btn.setStyleSheet("background-color:#3a3f44; color:white;")

    # ===== UPDATE =====
    def update_all(self):
        self.data = self.data[1:] + [random.randint(0,100)]
        for graph, curve in self.graphs:
            curve.setData(self.data)

        if self.recording and self.csv_writer:
            self.csv_writer.writerow([
                datetime.now().isoformat(),
                random.uniform(0.01,0.05),
                random.randint(50,70),
                random.uniform(0.1,0.5),
                random.randint(80,120),
                random.randint(1000,1500),
                random.uniform(0.0,1.0)
            ])
            self.csv_file.flush()

    # ===== RECORDING FUNCTIONS =====
    def toggle_recording(self):
        if not self.recording:
            os.makedirs("logs", exist_ok=True)
            filename = os.path.join("logs", f"rec_{datetime.now().strftime('%m%d_%H%M%S')}.csv")
            self.csv_file = open(filename, 'w', newline='')
            self.csv_writer = csv.writer(self.csv_file)
            self.csv_writer.writerow(['timestamp','rms_v','rms_a','cur','temp','rpm','d2'])
            self.csv_filename = filename
            self.recording = True
            
            self.rec_status_label.setText("● LOGGING...")
            self.rec_status_label.setStyleSheet("color:green; font-weight:bold;")
            
            self.rec_toggle_btn.setText("STOP RECORD")
            self.rec_toggle_btn.setStyleSheet("background-color:#ff0000; color:white; font-weight:bold; font-size:12px;")
        else:
            self.recording = False
            if self.csv_file:
                self.csv_file.close()
                
            self.rec_status_label.setText("● SAVED")
            self.rec_status_label.setStyleSheet("color:gray; font-weight:bold;")
            
            self.rec_toggle_btn.setText("START RECORD")
            self.rec_toggle_btn.setStyleSheet("background-color:#007acc; color:white; font-weight:bold; font-size:12px;")
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
            print("Opening:", filename)  # debug
            import pandas as pd
            import matplotlib.pyplot as plt
            df = pd.read_csv("logs/rec_0706_164820.csv")
            for col in ["rms_v","rms_a","cur","temp","rpm","d2"]:
                if col in df.columns:
                    df[col] = pd.to_numeric(df[col], errors="coerce")
            df = df.dropna()
            df.plot(x="timestamp", y=["rms_v","rms_a","cur","temp","rpm","d2"])
            plt.show()

    # ===== EXIT SHORTCUT FOR TESTING =====
    def keyPressEvent(self, event):
        # Tekan tombol 'Esc' di keyboard untuk keluar dari aplikasi fullscreen
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
