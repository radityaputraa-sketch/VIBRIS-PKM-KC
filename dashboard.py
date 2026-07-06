import sys, random, os, csv
from datetime import datetime
from PyQt5.QtWidgets import *
from PyQt5.QtCore import QTimer, Qt
import pyqtgraph as pg

class Dashboard(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Deteksi Dini Mesin Rotasi")
        self.setFixedSize(480, 320)  # pas untuk TFT 3.5"
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

        self.set_mode(0)

    # ===== PAGES =====
    def _page_raw(self):
        page = QWidget()
        grid = QGridLayout(page)
        grid.setContentsMargins(4,4,4,4)
        grid.setSpacing(4)
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
        rec_title.setStyleSheet("font-size:12px; font-weight:bold;")
        layout.addWidget(rec_title)

        self.rec_status_label = QLabel("● IDLE")
        self.rec_status_label.setStyleSheet("font-size:10px; font-weight:bold; color:red;")
        layout.addWidget(self.rec_status_label)

        self.rec_toggle_btn = QPushButton("START RECORD")
        self.rec_toggle_btn.setMinimumHeight(35)
        self.rec_toggle_btn.setStyleSheet("background-color:#007acc; color:white; font-weight:bold;")
        self.rec_toggle_btn.clicked.connect(self.toggle_recording)
        layout.addWidget(self.rec_toggle_btn)

        self.rec_list = QListWidget()
        self.rec_list.setStyleSheet("background-color:#1a1c1e; color:white; font-size:9px;")
        layout.addWidget(self.rec_list)

        self._refresh_log_list()
        return page

    def _page_processed(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setAlignment(Qt.AlignCenter)
        label = QLabel("Processed Reading")
        label.setStyleSheet("font-size:12px; font-weight:bold;")
        layout.addWidget(label)
        self.proc_info = QLabel("Data dibandingkan dengan nilai normal")
        self.proc_info.setStyleSheet("font-size:10px; color:#ffaa00;")
        layout.addWidget(self.proc_info)
        return page

    def _page_summary(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setAlignment(Qt.AlignCenter)
        self.sum_info = QLabel("Summary:\nVibration: WARNING\nSound: NORMAL\nTemp: HIGH\nCurrent: NORMAL")
        self.sum_info.setStyleSheet("font-size:11px; font-weight:bold;")
        layout.addWidget(self.sum_info)
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

        # tulis ke CSV jika recording aktif
        if self.recording and self.csv_writer:
            self.csv_writer.writerow([
                datetime.now().isoformat(),
                random.uniform(0.01,0.05),   # vibration dummy
                random.randint(50,70),       # sound dummy
                random.randint(80,120),      # temp dummy
                random.uniform(0.1,0.5)      # current dummy
            ])
            self.csv_file.flush()

    # ===== RECORDING FUNCTIONS =====
    def toggle_recording(self):
        if not self.recording:
            os.makedirs("logs", exist_ok=True)
            filename = os.path.join("logs", f"rec_{datetime.now().strftime('%m%d_%H%M%S')}.csv")
            self.csv_file = open(filename, 'w', newline='')
            self.csv_writer = csv.writer(self.csv_file)
            self.csv_writer.writerow(['timestamp','vibration','sound','temp','current'])
            self.csv_filename = filename
            self.recording = True
            self.rec_status_label.setText("● LOGGING...")
            self.rec_status_label.setStyleSheet("color:green; font-weight:bold;")
            self.rec_toggle_btn.setText("STOP RECORD")
            self.rec_toggle_btn.setStyleSheet("background-color:#dc3545; color:white;")
        else:
            self.recording = False
            if self.csv_file:
                self.csv_file.close()
            self.rec_status_label.setText("● SAVED")
            self.rec_status_label.setStyleSheet("color:gray; font-weight:bold;")
            self.rec_toggle_btn.setText("START RECORD")
            self.rec_toggle_btn.setStyleSheet("background-color:#007acc; color:white;")
            self._refresh_log_list()

    def _refresh_log_list(self):
        self.rec_list.clear()
        if os.path.isdir("logs"):
            for f in sorted(os.listdir("logs"), reverse=True):
                if f.endswith(".csv"):
                    self.rec_list.addItem(f)

    def closeEvent(self, event):
        if self.csv_file:
            self.csv_file.close()
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = Dashboard()
    win.show()
    sys.exit(app.exec_())
