import sys
import random
from PyQt5.QtWidgets import *
from PyQt5.QtCore import QTimer
import pyqtgraph as pg

class Dashboard(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("DETEKSI DINI MESIN ROTASI V1.0")
        # ukuran layar TFT Raspberry Pi 3.5" = 480x320
        self.setGeometry(0, 0, 480, 320)
        self.setStyleSheet("background-color: #cfcfcf;")

        # Layout utama
        self.main_layout = QHBoxLayout()
        self.setLayout(self.main_layout)

        # LEFT (Graph)
        self.left_layout = QGridLayout()
        self.main_layout.addLayout(self.left_layout, 2)

        # RIGHT (Info Panel)
        self.right_layout = QVBoxLayout()
        self.main_layout.addLayout(self.right_layout, 1)

        # MENU BUTTONS
        btn_layout = QGridLayout()
        self.btn_rec = QPushButton("Recording & Saves")
        self.btn_raw = QPushButton("Raw Reading")
        self.btn_proc = QPushButton("Processed Reading")
        self.btn_sum = QPushButton("Summary")

        # ukuran tombol lebih kecil biar muat
        for btn in [self.btn_rec, self.btn_raw, self.btn_proc, self.btn_sum]:
            btn.setFixedHeight(30)
            btn.setStyleSheet("font-size:10px;")

        self.btn_rec.setStyleSheet("background-color: orange; font-weight:bold; font-size:10px;")
        for btn in [self.btn_raw, self.btn_proc, self.btn_sum]:
            btn.setStyleSheet("background-color: #4a6fa5; color: white; font-size:10px;")

        buttons = [self.btn_rec, self.btn_raw, self.btn_proc, self.btn_sum]
        for i, btn in enumerate(buttons):
            btn_layout.addWidget(btn, i // 2, i % 2)

        self.right_layout.addLayout(btn_layout)

        # Connect
        self.btn_rec.clicked.connect(self.show_recording)
        self.btn_raw.clicked.connect(self.show_raw)
        self.btn_proc.clicked.connect(self.show_processed)
        self.btn_sum.clicked.connect(self.show_summary)

        # Data dummy
        self.data = [0]*50
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_data)
        self.timer.start(200)

        self.show_recording()

    def clear_left_panel(self):
        for i in reversed(range(self.left_layout.count())):
            widget = self.left_layout.itemAt(i).widget()
            if widget: widget.deleteLater()

    def clear_right_panel(self):
        for i in reversed(range(self.right_layout.count())):
            item = self.right_layout.itemAt(i)
            if item and item.layout() is None:
                widget = item.widget()
                if widget: widget.deleteLater()

    def update_data(self):
        self.data = self.data[1:] + [random.randint(0, 100)]
        for i in range(self.left_layout.count()):
            widget = self.left_layout.itemAt(i).widget()
            if isinstance(widget, pg.PlotWidget):
                curves = widget.listDataItems()
                if curves: curves[0].setData(self.data)

    # ===== MENU PAGES =====
    def show_recording(self):
        self.clear_left_panel(); self.clear_right_panel()
        controls = QHBoxLayout()
        for txt in ["Start","Pause","●"]:
            b = QPushButton(txt); b.setFixedSize(60,25); controls.addWidget(b)
        self.right_layout.addLayout(controls)
        saves = QLabel("Save 1 : Electric Fan\nSave 2 : Servo Motor\nSave 3 : Drill\nSave 4 : Blender\nSave 5 : Empty\nSave 6 : Empty")
        saves.setStyleSheet("font-size:10px;")
        self.right_layout.addWidget(saves)

    def show_raw(self):
        self.clear_left_panel(); self.clear_right_panel()
        titles = ["Vibration","Sound","Temperature","Current"]
        for i in range(4):
            g = pg.PlotWidget(); g.setBackground('w'); g.setTitle(titles[i],color='b',size='10pt')
            g.plot(pen='r'); self.left_layout.addWidget(g,i//2,i%2)
        self.right_layout.addWidget(QLabel("[Raw Reading]\nData sensor mentah realtime"))

    def show_processed(self):
        self.clear_left_panel(); self.clear_right_panel()
        g1 = pg.PlotWidget(); g1.setBackground('w'); g1.setTitle("Vibration (Cur vs Norm)",color='b',size='10pt')
        g1.plot([random.randint(0,50) for _ in range(50)],pen='r'); g1.plot([25]*50,pen='g')
        self.left_layout.addWidget(g1,0,0)
        g2 = pg.PlotWidget(); g2.setBackground('w'); g2.setTitle("Sound (Cur vs Norm)",color='b',size='10pt')
        g2.plot([random.randint(40,70) for _ in range(50)],pen='r'); g2.plot([55]*50,pen='g')
        self.left_layout.addWidget(g2,0,1)
        temp = round(random.uniform(80,130),1); curr = 0.02
        lbl = QLabel(f"Temperature : {temp} °C\nCurrent : {curr:.2f} A")
        lbl.setStyleSheet("font-size:10px; font-weight:bold;")
        self.right_layout.addWidget(lbl)

    def show_summary(self):
        self.clear_left_panel(); self.clear_right_panel()

        def make_bar(name, level, value, color):
            bar = QProgressBar()
            bar.setRange(0,100)
            bar.setValue(value)
            bar.setStyleSheet(f"QProgressBar::chunk {{background-color:{color};}} font-size:10px;")
            bar.setFormat(f"{name} : {level}")
            return bar

        self.right_layout.addWidget(make_bar("Vibration","Moderate",70,"yellow"))
        self.right_layout.addWidget(make_bar("Sound","Minor",60,"lightblue"))
        self.right_layout.addWidget(make_bar("Temperature","Extreme",95,"red"))
        self.right_layout.addWidget(make_bar("Current","Normal",50,"lightgreen"))

if __name__=="__main__":
    app=QApplication(sys.argv)
    win=Dashboard(); win.show()
    sys.exit(app.exec_())
