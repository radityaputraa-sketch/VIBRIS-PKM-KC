import sys
import json
import serial
from PyQt5.QtWidgets import QApplication, QWidget, QVBoxLayout, QLabel, QHBoxLayout
from PyQt5.QtCore import QTimer
import pyqtgraph as pg

class Dashboard(QWidget):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("VIBRIS Dashboard")
        self.setGeometry(100, 100, 1000, 600)

        try:
            self.ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
            print("[INFO] Serial connected")
        except:
            self.ser = None
            print("[WARNING] Serial NOT connected")

        self.data_vib = [0]*50
        self.data_sound = [0]*50
        self.data_temp = [0]*50
        self.data_current = [0]*50

        main_layout = QVBoxLayout()
        self.setLayout(main_layout)

        self.status_label = QLabel("STATUS: -")
        self.status_label.setStyleSheet("font-size: 20px; font-weight: bold;")
        main_layout.addWidget(self.status_label)

        graph_layout = QHBoxLayout()
        main_layout.addLayout(graph_layout)

        self.plot_vib = pg.PlotWidget(title="Vibration")
        self.curve_vib = self.plot_vib.plot(self.data_vib)

        self.plot_sound = pg.PlotWidget(title="Sound")
        self.curve_sound = self.plot_sound.plot(self.data_sound)

        self.plot_temp = pg.PlotWidget(title="Temperature")
        self.curve_temp = self.plot_temp.plot(self.data_temp)

        self.plot_current = pg.PlotWidget(title="Current")
        self.curve_current = self.plot_current.plot(self.data_current)

        graph_layout.addWidget(self.plot_vib)
        graph_layout.addWidget(self.plot_sound)
        graph_layout.addWidget(self.plot_temp)
        graph_layout.addWidget(self.plot_current)

        self.timer = QTimer()
        self.timer.timeout.connect(self.update_data)
        self.timer.start(200)

    def update_data(self):
        if self.ser is None:
            return

        try:
            line = self.ser.readline().decode().strip()

            if not line.startswith("{"):
                return

            data = json.loads(line)

            vib = data.get("getaran", 0)
            sound = data.get("suara", 0)
            temp = data.get("suhu", 0)
            current = data.get("arus", 0)
            status = data.get("status", "UNKNOWN")

            self.data_vib = self.data_vib[1:] + [vib]
            self.data_sound = self.data_sound[1:] + [sound]
            self.data_temp = self.data_temp[1:] + [temp]
            self.data_current = self.data_current[1:] + [current]

            self.curve_vib.setData(self.data_vib)
            self.curve_sound.setData(self.data_sound)
            self.curve_temp.setData(self.data_temp)
            self.curve_current.setData(self.data_current)

            self.status_label.setText(f"STATUS: {status}")

            if status == "NORMAL":
                self.status_label.setStyleSheet("color: green; font-size: 20px;")
            elif status == "WARNING":
                self.status_label.setStyleSheet("color: orange; font-size: 20px;")
            elif status == "DANGER":
                self.status_label.setStyleSheet("color: red; font-size: 20px;")
            else:
                self.status_label.setStyleSheet("color: white; font-size: 20px;")

        except Exception as e:
            print("[ERROR]", e)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = Dashboard()
    window.show()
    sys.exit(app.exec_())
