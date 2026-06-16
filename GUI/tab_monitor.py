"""
tab_monitor.py
--------------
Live Monitor tab — real-time injection log, metrics, and charts.
Uses QWebEngineView to render a Chart.js radar chart.
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel,
    QTextEdit, QFrame, QSizePolicy, QProgressBar, QPushButton
)
from PyQt5.QtCore import Qt, pyqtSignal, QUrl
from PyQt5.QtGui import QPainter, QColor, QFont
from PyQt5.QtWebEngineWidgets import QWebEngineView

import json
import styles
from widgets import MetricCard, CardFrame, CardTitle, EmptyState
from config import FaultConfig, FAULT_TYPES, HARDWARE_MODES, ASIL_FTTI_MS


# ── Smart chart: gauge for 1 fault type, radar for 2+ ───────────────────────
def _chart_html(green, amber, red, text):
    return """<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body { background:transparent; display:flex; align-items:center; justify-content:center; height:100vh; overflow:hidden; }
  #wrap { width:100%; height:100%; display:flex; flex-direction:column; align-items:center; justify-content:center; padding:16px 24px; }
  canvas { max-width:100%; max-height:100%; }
  #gauge { display:none; width:100%; }
  #gauge-label { font-size:14px; font-family:Arial,sans-serif; margin-bottom:14px; font-weight:600; }
  #gauge-track { width:100%; height:22px; background:rgba(255,255,255,0.08); border-radius:11px; overflow:hidden; }
  #gauge-fill { height:100%; border-radius:11px; transition:width 0.5s ease; }
  #gauge-bottom { display:flex; align-items:baseline; gap:10px; margin-top:12px; }
  #gauge-pct { font-size:28px; font-weight:700; font-family:Arial,sans-serif; }
  #gauge-sub { font-size:13px; font-family:Arial,sans-serif; opacity:0.55; }
  #gauge-ticks { display:flex; justify-content:space-between; margin-top:4px; font-size:11px; font-family:Arial,sans-serif; opacity:0.4; }
</style>
</head>
<body>
<div id="wrap">
  <canvas id="radar" role="img" aria-label="Radar chart showing fault handling by type" style="display:none"></canvas>
  <div id="gauge">
    <div id="gauge-label" style="color:""" + text + """">Waiting for results...</div>
    <div id="gauge-track"><div id="gauge-fill" style="width:0%;background:""" + green + """"></div></div>
    <div id="gauge-ticks" style="color:""" + text + """"><span>0%</span><span>25%</span><span>50%</span><span>75%</span><span>100%</span></div>
    <div id="gauge-bottom">
      <span id="gauge-pct" style="color:""" + green + """">0%</span>
      <span id="gauge-sub" style="color:""" + text + """">handling rate</span>
    </div>
  </div>
</div>
<script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.js"></script>
<script>
var chart = null;
var GREEN="__GREEN__", AMBER="__AMBER__", RED="__RED__", TEXT="__TEXT__";
function pickColor(v) { return v>=90?GREEN:v>=70?AMBER:RED; }

function showGauge(label, pct) {
  document.getElementById("radar").style.display="none";
  document.getElementById("gauge").style.display="block";
  var color = pickColor(pct);
  document.getElementById("gauge-label").textContent = label;
  document.getElementById("gauge-fill").style.width = pct+"%";
  document.getElementById("gauge-fill").style.background = color;
  document.getElementById("gauge-pct").textContent = pct+"%";
  document.getElementById("gauge-pct").style.color = color;
}

function showRadar(labels, values) {
  document.getElementById("gauge").style.display="none";
  document.getElementById("radar").style.display="block";
  var bColors = values.map(pickColor);
  var avg = bColors[0]||GREEN;
  var bg = avg===GREEN?"rgba(29,158,117,0.15)":avg===AMBER?"rgba(240,165,0,0.15)":"rgba(231,76,60,0.15)";
  if(chart){chart.destroy();chart=null;}
  chart = new Chart(document.getElementById("radar").getContext("2d"),{
    type:"radar",
    data:{labels:labels,datasets:[{data:values,backgroundColor:bg,borderColor:avg,
      pointBackgroundColor:bColors,pointBorderColor:"transparent",pointRadius:5,pointHoverRadius:7,borderWidth:2}]},
    options:{responsive:true,maintainAspectRatio:true,
      plugins:{legend:{display:false},tooltip:{callbacks:{label:function(c){return " "+c.raw+"% handled";}}}},
      scales:{r:{min:0,max:100,
        ticks:{stepSize:25,color:TEXT,font:{size:11},backdropColor:"transparent",callback:function(v){return v+"%";}},
        grid:{color:"rgba(255,255,255,0.08)"},angleLines:{color:"rgba(255,255,255,0.08)"},
        pointLabels:{color:TEXT,font:{size:12,weight:"500"}}}}}
  });
}

function updateChart(labelsJson, valuesJson) {
  var labels=JSON.parse(labelsJson), values=JSON.parse(valuesJson);
  if(labels.length===1){ showGauge(labels[0],values[0]); }
  else { showRadar(labels,values); }
}

// Default: show gauge waiting state
document.getElementById("gauge").style.display="block";
</script>
</body>
</html>""".replace("__GREEN__", green).replace("__AMBER__", amber).replace("__RED__", red).replace("__TEXT__", text)


class RadarChart(QWebEngineView):
    """Chart.js radar chart embedded in a QWebEngineView."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(170)
        self.setMaximumHeight(210)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        # Transparent background so it blends with the dark card
        self.page().setBackgroundColor(Qt.transparent)
        self.setHtml(_chart_html(styles.ACCENT_GREEN, styles.ACCENT_AMBER, styles.ACCENT_RED, '#9e9e9e'))
        self._pending_labels = None
        self._pending_values = None
        self.loadFinished.connect(self._on_loaded)
        self._loaded = False

    def _on_loaded(self, ok):
        self._loaded = True
        if self._pending_labels is not None:
            self._push(self._pending_labels, self._pending_values)
            self._pending_labels = None
            self._pending_values = None

    def update_data(self, labels: list, values: list):
        if not self._loaded:
            self._pending_labels = labels
            self._pending_values = values
            return
        self._push(labels, values)

    def _push(self, labels: list, values: list):
        lj = json.dumps(labels)
        vj = json.dumps(values)
        self.page().runJavaScript(f"updateChart({repr(lj)}, {repr(vj)});")


# ── Donut chart ───────────────────────────────────────────────────────────────
class DonutWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._value = 0.0
        self._label = "0%"
        self.setMinimumSize(140, 140)
        self.setMaximumSize(160, 160)
        self.setStyleSheet("background: transparent;")

    def setValue(self, pct: float):
        self._value = pct
        self._label = f"{pct:.1f}%"
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        cx, cy = w // 2, h // 2
        r = min(w, h) // 2 - 10
        thickness = 16
        painter.setPen(Qt.NoPen)
        painter.setBrush(QColor(styles.BG_CARD))
        painter.drawEllipse(cx - r, cy - r, r * 2, r * 2)
        painter.setBrush(QColor(styles.BG_SECONDARY))
        painter.drawEllipse(cx - r + thickness, cy - r + thickness,
                            (r - thickness) * 2, (r - thickness) * 2)
        from PyQt5.QtGui import QPen
        span = int(self._value / 100 * 360 * 16)
        color = styles.ACCENT_GREEN if self._value >= 90 else (
            styles.ACCENT_AMBER if self._value >= 70 else styles.ACCENT_RED)
        pen = QPen(QColor(color))
        pen.setWidth(thickness)
        pen.setCapStyle(Qt.RoundCap)
        painter.setPen(pen)
        painter.drawArc(
            int(cx - r + thickness // 2), int(cy - r + thickness // 2),
            int((r - thickness // 2) * 2), int((r - thickness // 2) * 2),
            90 * 16, -span
        )
        painter.setPen(QColor(styles.TEXT_PRIMARY))
        font = QFont()
        font.setPointSize(13)
        font.setBold(True)
        painter.setFont(font)
        painter.drawText(0, 0, w, h, Qt.AlignCenter, self._label)


# ── Confirmation banner ───────────────────────────────────────────────────────
class BigConfirmBanner(QWidget):
    confirmed      = pyqtSignal()
    edit_requested = pyqtSignal()

    def __init__(self, message: str, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(100)
        self.setMaximumHeight(130)
        self._apply_active_style()

        outer = QVBoxLayout(self)
        outer.setContentsMargins(18, 14, 18, 14)
        outer.setSpacing(10)

        title_row = QHBoxLayout()
        title_row.setSpacing(10)
        self._icon = QLabel("⚠")
        self._icon.setStyleSheet(
            f"font-size: 22px; color: {styles.ACCENT_AMBER}; background: transparent; border: none;"
        )
        self._icon.setFixedWidth(30)
        self._title = QLabel("Confirm before running")
        self._title.setStyleSheet(
            f"font-size: 14px; font-weight: bold; color: {styles.ACCENT_AMBER}; background: transparent; border: none;"
        )
        title_row.addWidget(self._icon)
        title_row.addWidget(self._title)
        title_row.addStretch()

        content_row = QHBoxLayout()
        content_row.setSpacing(14)

        self._msg = QLabel(message)
        self._msg.setWordWrap(True)
        self._msg.setTextFormat(Qt.RichText)
        self._msg.setStyleSheet(
            f"color: #d4a84b; font-size: 12px; background: transparent; border: none;"
        )

        btn_col = QVBoxLayout()
        btn_col.setSpacing(6)
        btn_col.setAlignment(Qt.AlignVCenter)

        self._confirm_btn = QPushButton("✓  Confirm & run")
        self._confirm_btn.setFixedSize(170, 36)
        self._confirm_btn.setStyleSheet(f"""
            QPushButton {{
                background: {styles.ACCENT_GREEN}; color: white;
                border: none; border-radius: 6px;
                font-size: 12px; font-weight: bold;
            }}
            QPushButton:hover {{ background: #25b887; }}
            QPushButton:pressed {{ background: #178a63; }}
        """)
        self._confirm_btn.clicked.connect(self._on_confirm)

        self._edit_btn = QPushButton("✏  Edit configuration")
        self._edit_btn.setFixedSize(170, 36)
        self._edit_btn.setStyleSheet(f"""
            QPushButton {{
                background: transparent; color: {styles.TEXT_SECONDARY};
                border: 1px solid {styles.BORDER_COLOR}; border-radius: 6px;
                font-size: 12px; font-weight: bold;
            }}
            QPushButton:hover {{ background: {styles.BG_CARD}; color: {styles.TEXT_PRIMARY}; }}
        """)
        self._edit_btn.clicked.connect(self._on_edit)

        btn_col.addWidget(self._confirm_btn)
        btn_col.addWidget(self._edit_btn)
        content_row.addWidget(self._msg, stretch=1)
        content_row.addLayout(btn_col)

        outer.addLayout(title_row)
        outer.addLayout(content_row)

    def _apply_active_style(self):
        self.setStyleSheet(f"""
            QWidget {{
                background: #2a1f00;
                border: 2px solid {styles.ACCENT_AMBER};
                border-radius: 10px;
            }}
        """)

    def _apply_dimmed_style(self):
        self.setStyleSheet(f"""
            QWidget {{
                background: {styles.BG_SECONDARY};
                border: 2px solid {styles.BORDER_COLOR};
                border-radius: 10px;
            }}
        """)

    def _on_confirm(self):
        self.confirmed.emit()

    def _on_edit(self):
        self._apply_dimmed_style()
        self._msg.setStyleSheet(f"color: {styles.TEXT_DISABLED}; font-size: 12px; background: transparent; border: none;")
        self._title.setStyleSheet(f"font-size: 14px; font-weight: bold; color: {styles.TEXT_DISABLED}; background: transparent; border: none;")
        self._icon.setStyleSheet(f"font-size: 22px; color: {styles.TEXT_DISABLED}; background: transparent; border: none;")
        self._edit_btn.setEnabled(False)
        self._confirm_btn.setEnabled(False)
        self.edit_requested.emit()


# ── Monitor tab ───────────────────────────────────────────────────────────────
class MonitorTab(QWidget):
    confirmed      = pyqtSignal()
    edit_requested = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._root = QVBoxLayout(self)
        self._root.setContentsMargins(14, 14, 14, 14)
        self._root.setSpacing(10)
        self._show_empty()

    def _show_empty(self):
        self._clear()
        empty = EmptyState(
            "📡",
            "No active injection",
            "Configure a test on the left and press Run —\n"
            "live output, charts, and the injection log will appear here."
        )
        self._root.addWidget(empty)

    def show_running(self, config: FaultConfig, on_confirm, on_edit):
        self._clear()
        fault_label = FAULT_TYPES.get(config.fault_type, config.fault_type)
        hw_label    = HARDWARE_MODES.get(config.hardware, config.hardware)

        banner_msg = (
            f"<b>Hardware:</b> {hw_label} &nbsp;·&nbsp; "
            f"<b>Sensor:</b> {config.sensor} &nbsp;·&nbsp; "
            f"<b>Fault:</b> {fault_label} &nbsp;·&nbsp; "
            f"<b>Target:</b> {config.variable or config.address} &nbsp;·&nbsp; "
            f"<b>Duration:</b> {config.duration_s}s &nbsp;·&nbsp; "
            f"<b>ASIL:</b> {config.asil_level}"
        )

        self._banner = BigConfirmBanner(banner_msg)
        self._banner.confirmed.connect(on_confirm)
        self._banner.edit_requested.connect(on_edit)
        self._root.addWidget(self._banner)

        # ── Metric cards ──
        metrics_row = QHBoxLayout()
        metrics_row.setSpacing(10)
        self._m_tests    = MetricCard("Tests run",   "0", "This campaign",  styles.ACCENT_BLUE)
        self._m_detected = MetricCard("Handled",     "0", "of 0 faults",    styles.ACCENT_GREEN)
        self._m_coverage = MetricCard("Handling rate","—", "ASIL-D: ≥90%",  styles.TEXT_SECONDARY)
        _deadline = ASIL_FTTI_MS.get(config.asil_level, 10)
        self._m_latency  = MetricCard("ASIL deadline", f"{_deadline}ms", config.asil_level, styles.ACCENT_AMBER)
        for m in [self._m_tests, self._m_detected, self._m_coverage, self._m_latency]:
            metrics_row.addWidget(m)
        self._root.addLayout(metrics_row)

        # ── Charts row: radar (left) + donut (right) ──
        charts_row = QHBoxLayout()
        charts_row.setSpacing(10)

        # Radar card
        radar_card = CardFrame()
        radar_card.addWidget(CardTitle("Fault handling by type"))
        self._radar = RadarChart()
        radar_card.addWidget(self._radar)
        charts_row.addWidget(radar_card, stretch=3)

        # Donut card
        donut_card = QFrame()
        donut_card.setStyleSheet(f"""
            QFrame {{
                background: {styles.BG_SECONDARY};
                border: 1px solid {styles.BORDER_COLOR};
                border-radius: 8px;
            }}
        """)
        donut_outer = QVBoxLayout(donut_card)
        donut_outer.setContentsMargins(14, 12, 14, 12)
        donut_outer.setSpacing(0)

        # Title pinned to top-left
        donut_title = CardTitle("Overall handling rate")
        donut_outer.addWidget(donut_title, alignment=Qt.AlignTop | Qt.AlignLeft)

        # Center area: donut + legend side by side, fully centered H+V
        donut_center = QHBoxLayout()
        donut_center.setSpacing(16)
        donut_center.setAlignment(Qt.AlignCenter)

        self._donut = DonutWidget()

        legend_col = QVBoxLayout()
        legend_col.setSpacing(10)
        legend_col.setAlignment(Qt.AlignVCenter)
        self._legend_detected = QLabel("● Safe (0)")
        self._legend_detected.setStyleSheet(
            f"color: {styles.ACCENT_GREEN}; font-size: 13px; font-weight: bold; background: transparent;"
        )
        self._legend_missed = QLabel("● Unsafe (0)")
        self._legend_missed.setStyleSheet(
            f"color: {styles.TEXT_DISABLED}; font-size: 13px; background: transparent;"
        )
        legend_col.addWidget(self._legend_detected)
        legend_col.addWidget(self._legend_missed)

        donut_center.addWidget(self._donut)
        donut_center.addLayout(legend_col)
        donut_outer.addLayout(donut_center, stretch=1)
        charts_row.addWidget(donut_card, stretch=2)

        self._root.addLayout(charts_row)

        # ── Progress bar ──
        prog_card = CardFrame()
        prog_layout = QHBoxLayout()
        prog_layout.setSpacing(12)
        self._progress_lbl = QLabel("Waiting for confirmation...")
        self._progress_lbl.setStyleSheet(
            f"color: {styles.TEXT_SECONDARY}; font-size: 12px; background: transparent;"
        )
        self._progress_lbl.setMinimumWidth(180)
        self._progress_bar = QProgressBar()
        self._progress_bar.setRange(0, 100)
        self._progress_bar.setValue(0)
        self._progress_bar.setFixedHeight(10)
        prog_layout.addWidget(self._progress_lbl)
        prog_layout.addWidget(self._progress_bar, stretch=1)
        prog_card.addLayout(prog_layout)
        self._root.addWidget(prog_card)

        # ── Log ──
        log_card = CardFrame()
        log_card.addWidget(CardTitle("⌨  Injection log"))
        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setMinimumHeight(200)
        self._log.setMaximumHeight(400)
        log_card.addWidget(self._log)
        self._root.addWidget(log_card, stretch=1)

        self._total       = 0
        self._detected    = 0
        self._fault_label = FAULT_TYPES.get(config.fault_type, config.fault_type)
        self._fault_stats = {}

    def append_log(self, line: str):
        color = (
            styles.ACCENT_GREEN if ("✓" in line or "OK" in line or "SAFE STATE" in line)
            else styles.ACCENT_RED if ("✗" in line or "ERROR" in line)
            else styles.ACCENT_AMBER if "WARN" in line
            else "#58a6ff"
        )
        self._log.append(
            f'<span style="color:{color}; font-family:Consolas,monospace; font-size:12px;">{line}</span>'
        )
        self._log.verticalScrollBar().setValue(self._log.verticalScrollBar().maximum())

        if "✓" in line and "SAFE STATE" in line:
            self._detected += 1
            self._total += 1
            ft = self._fault_label
            if ft not in self._fault_stats:
                self._fault_stats[ft] = {"total": 0, "detected": 0}
            self._fault_stats[ft]["total"] += 1
            self._fault_stats[ft]["detected"] += 1
            self._refresh_metrics()
        elif "✗" in line and "SYSTEM FAILED" in line:
            self._total += 1
            ft = self._fault_label
            if ft not in self._fault_stats:
                self._fault_stats[ft] = {"total": 0, "detected": 0}
            self._fault_stats[ft]["total"] += 1
            self._refresh_metrics()

    def set_progress(self, pct: int):
        self._progress_bar.setValue(pct)
        self._progress_lbl.setText(f"Running... {pct}%")

    def set_complete(self):
        self._progress_lbl.setText("✓  Campaign complete")
        self._progress_bar.setValue(100)

    def _refresh_metrics(self):
        if self._total == 0:
            return
        coverage = round((self._detected / self._total) * 100, 1)
        self._m_tests.setValue(str(self._total), styles.ACCENT_BLUE)
        self._m_detected.setValue(str(self._detected), styles.ACCENT_GREEN)
        self._m_detected.setSub(f"of {self._total} faults")
        cov_color = (
            styles.ACCENT_GREEN if coverage >= 90
            else styles.ACCENT_AMBER if coverage >= 70
            else styles.ACCENT_RED
        )
        self._m_coverage.setValue(f"{coverage}%", cov_color)
        self._donut.setValue(coverage)
        self._legend_detected.setText(f"● Safe ({self._detected})")
        self._legend_missed.setText(f"● Unsafe ({self._total - self._detected})")

        # Push updated data to radar chart
        labels, values = [], []
        for ft, stats in self._fault_stats.items():
            pct = round(stats["detected"] / stats["total"] * 100) if stats["total"] else 0
            labels.append(ft[:12])
            values.append(pct)
        if labels:
            self._radar.update_data(labels, values)

    def hide_banner(self):
        if hasattr(self, "_banner"):
            self._banner.hide()

    def _clear(self):
        while self._root.count():
            item = self._root.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
            elif item.layout():
                self._clear_layout(item.layout())

    def _clear_layout(self, layout):
        while layout.count():
            item = layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()