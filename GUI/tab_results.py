"""
tab_results.py
--------------
Results tab — final metrics, charts, and test case breakdown table.
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel,
    QTableWidget, QTableWidgetItem, QHeaderView,
    QFrame, QSizePolicy, QScrollArea, QProgressBar
)
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QColor, QFont, QPainter

import styles
from widgets import MetricCard, CardTitle, EmptyState, StatusBadge


# ── Simple vertical bar chart (for latency histogram) ────────────────────────
class SimpleBarChart(QWidget):
    def __init__(self, labels, values, colors, parent=None):
        super().__init__(parent)
        self.labels = labels
        self.values = values
        self.colors = colors
        self.setStyleSheet("background: transparent;")

    def paintEvent(self, event):
        if not self.values:
            return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        max_val = max(self.values) if max(self.values) > 0 else 1
        n = len(self.values)
        bar_w = max(int(w / (n * 2.2)), 14)
        spacing = (w - bar_w * n) // (n + 1)
        for i, (val, color) in enumerate(zip(self.values, self.colors)):
            bar_h = int((val / max_val) * (h - 28)) if max_val > 0 else 0
            x = spacing + i * (bar_w + spacing)
            y = h - 22 - bar_h
            painter.setBrush(QColor(color))
            painter.setPen(Qt.NoPen)
            painter.drawRoundedRect(x, y, bar_w, bar_h, 4, 4)
            painter.setPen(QColor(styles.TEXT_SECONDARY))
            font = QFont()
            font.setPointSize(9)
            painter.setFont(font)
            painter.drawText(x - 4, h - 4, self.labels[i][:5])


def _make_card() -> tuple:
    """Returns (QFrame card, QVBoxLayout outer) with title-friendly layout."""
    card = QFrame()
    card.setStyleSheet(f"""
        QFrame {{
            background: {styles.BG_SECONDARY};
            border: 1px solid {styles.BORDER_COLOR};
            border-radius: 8px;
        }}
    """)
    layout = QVBoxLayout(card)
    layout.setContentsMargins(14, 12, 14, 12)
    layout.setSpacing(8)
    return card, layout


class ResultsTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._root = QVBoxLayout(self)
        self._root.setContentsMargins(14, 14, 14, 14)
        self._root.setSpacing(12)
        self._show_empty()

    def _show_empty(self):
        self._clear()
        empty = EmptyState(
            "📊",
            "No results yet",
            "Results, charts, and the full test case breakdown\n"
            "will appear here once a campaign has been run."
        )
        self._root.addWidget(empty)

    def show_results(self, data: dict):
        self._clear()

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setStyleSheet("background: transparent;")

        container = QWidget()
        container.setStyleSheet("background: transparent;")
        vl = QVBoxLayout(container)
        vl.setContentsMargins(0, 0, 0, 0)
        vl.setSpacing(12)

        cfg      = data["config"]
        total    = data["total"]
        detected = data["detected"]
        coverage = data["coverage_pct"]
        avg_lat  = data["avg_latency_ms"]
        max_lat  = data["max_latency_ms"]
        min_lat  = data["min_latency_ms"]
        overhead = data["overhead_pct"]
        ftti     = data.get("ftti_ms", "—")
        cases    = data["test_cases"]
        passed   = data["passed_asil"]

        # ── Metric cards ──────────────────────────────────────────────────────
        metrics_row = QHBoxLayout()
        metrics_row.setSpacing(10)
        cov_color = styles.ACCENT_GREEN if coverage >= 90 else (
            styles.ACCENT_AMBER if coverage >= 70 else styles.ACCENT_RED)
        oh_color = styles.ACCENT_GREEN if overhead < 5 else styles.ACCENT_RED

        for label, value, sub, color in [
            ("Total tests",    str(total),       f"{cfg.sensor} campaign",            styles.ACCENT_BLUE),
            ("Handling rate",  f"{coverage}%",   f"{cfg.asil_level}: {'PASS ✓' if passed else 'FAIL ✗'}", cov_color),
            ("ASIL deadline",  f"{ftti}ms",      f"{cfg.asil_level} recovery limit", styles.ACCENT_AMBER),
            ("Overhead",       f"{overhead}%",   "Target: <5% " + ("✓" if overhead < 5 else "✗"), oh_color),
        ]:
            metrics_row.addWidget(MetricCard(label, value, sub, color))
        vl.addLayout(metrics_row)

        # ── Charts row ────────────────────────────────────────────────────────
        charts_row = QHBoxLayout()
        charts_row.setSpacing(10)

        # ── Detection by fault type card ──
        bar_card, bar_outer = _make_card()
        bar_outer.addWidget(CardTitle("Fault handling by type"),
                            alignment=Qt.AlignTop | Qt.AlignLeft)

        fault_stats = {}
        for tc in cases:
            ft = tc["fault_type"]
            if ft not in fault_stats:
                fault_stats[ft] = {"total": 0, "detected": 0}
            fault_stats[ft]["total"] += 1
            if tc["outcome"] == "Pass":
                fault_stats[ft]["detected"] += 1

        bars_widget = QWidget()
        bars_widget.setStyleSheet("background: transparent;")
        bars_vl = QVBoxLayout(bars_widget)
        bars_vl.setContentsMargins(0, 0, 0, 0)
        bars_vl.setSpacing(10)
        bars_vl.setAlignment(Qt.AlignVCenter)

        for ft, stats in fault_stats.items():
            pct = round(stats["detected"] / stats["total"] * 100) if stats["total"] else 0
            fill_color = (styles.ACCENT_GREEN if pct >= 90
                          else styles.ACCENT_AMBER if pct >= 70
                          else styles.ACCENT_RED)

            row = QHBoxLayout()
            row.setSpacing(12)
            row.setAlignment(Qt.AlignVCenter)

            lbl = QLabel(ft[:16])
            lbl.setFixedWidth(110)
            lbl.setAlignment(Qt.AlignVCenter | Qt.AlignLeft)
            lbl.setStyleSheet(
                f"color: {styles.TEXT_SECONDARY}; font-size: 13px; background: transparent;"
            )

            bar = QProgressBar()
            bar.setRange(0, 100)
            bar.setValue(pct)
            bar.setFixedHeight(16)
            bar.setTextVisible(False)
            bar.setStyleSheet(f"""
                QProgressBar {{
                    background: {styles.BG_INPUT};
                    border: none; border-radius: 6px;
                }}
                QProgressBar::chunk {{
                    background: {fill_color}; border-radius: 6px;
                }}
            """)

            val_lbl = QLabel(f"{pct}%")
            val_lbl.setFixedWidth(42)
            val_lbl.setAlignment(Qt.AlignVCenter | Qt.AlignRight)
            val_lbl.setStyleSheet(
                f"color: {fill_color}; font-size: 13px; font-weight: bold; background: transparent;"
            )

            row.addWidget(lbl)
            row.addWidget(bar, stretch=1)
            row.addWidget(val_lbl)
            bars_vl.addLayout(row)

        bar_outer.addWidget(bars_widget, stretch=1)
        charts_row.addWidget(bar_card, stretch=3)

        # ── Latency distribution card ──
        lat_card, lat_outer = _make_card()
        lat_outer.addWidget(CardTitle("Reaction time distribution (ms)"),
                            alignment=Qt.AlignTop | Qt.AlignLeft)

        latencies = [tc["latency_ms"] for tc in cases if tc["latency_ms"]]
        buckets = [0, 0, 0, 0, 0]
        labels_h = ["0–2", "2–4", "4–6", "6–8", ">8"]
        for lat in latencies:
            if lat < 2:   buckets[0] += 1
            elif lat < 4: buckets[1] += 1
            elif lat < 6: buckets[2] += 1
            elif lat < 8: buckets[3] += 1
            else:         buckets[4] += 1

        lat_inner = QWidget()
        lat_inner.setStyleSheet("background: transparent;")
        lat_vl = QVBoxLayout(lat_inner)
        lat_vl.setContentsMargins(0, 0, 0, 0)
        lat_vl.setSpacing(8)
        lat_vl.setAlignment(Qt.AlignVCenter)

        if latencies:
            chart = SimpleBarChart(
                labels_h, buckets,
                [styles.ACCENT_BLUE, styles.ACCENT_BLUE, styles.ACCENT_BLUE,
                 styles.ACCENT_AMBER, styles.ACCENT_RED]
            )
            chart.setMinimumHeight(130)
            lat_vl.addWidget(chart)
        else:
            # Injector did not report per-fault reaction times — show a clear
            # placeholder instead of an empty (all-zero) bar chart.
            no_timing = QLabel("No per-fault reaction time data\n"
                               "(injector enforces the FTTI deadline internally)")
            no_timing.setAlignment(Qt.AlignCenter)
            no_timing.setWordWrap(True)
            no_timing.setMinimumHeight(130)
            no_timing.setStyleSheet(
                f"color: {styles.TEXT_DISABLED}; font-size: 12px; background: transparent;"
            )
            lat_vl.addWidget(no_timing)

        stats_row = QHBoxLayout()
        stats_row.setAlignment(Qt.AlignHCenter)
        for label, val in [("Min", f"{min_lat}ms"), ("Avg", f"{avg_lat}ms"), ("Max", f"{max_lat}ms")]:
            s = QLabel(f"{label}:  <b>{val}</b>")
            s.setAlignment(Qt.AlignCenter)
            s.setStyleSheet(
                f"color: {styles.TEXT_SECONDARY}; font-size: 12px; background: transparent;"
            )
            stats_row.addWidget(s, stretch=1)
        lat_vl.addLayout(stats_row)

        lat_outer.addWidget(lat_inner, stretch=1)
        charts_row.addWidget(lat_card, stretch=2)

        # Wrap charts row with max height so table gets more room
        charts_wrap = QWidget()
        charts_wrap.setStyleSheet("background: transparent;")
        charts_wrap.setLayout(charts_row)
        charts_wrap.setMaximumHeight(240)
        vl.addWidget(charts_wrap)

        # ── Test case table ───────────────────────────────────────────────────
        tbl_card, tbl_outer = _make_card()
        tbl_outer.addWidget(CardTitle("Test case breakdown"),
                            alignment=Qt.AlignTop | Qt.AlignLeft)

        table = QTableWidget()
        table.setColumnCount(6)
        table.setHorizontalHeaderLabels(
            ["ID", "Fault type", "Variable", "Injected", "System response", "Outcome"]
        )
        table.horizontalHeader().setSectionResizeMode(4, QHeaderView.Stretch)
        table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeToContents)
        table.setEditTriggers(QTableWidget.NoEditTriggers)
        table.setSelectionBehavior(QTableWidget.SelectRows)
        table.setAlternatingRowColors(False)
        table.verticalHeader().setVisible(False)
        table.setShowGrid(False)

        table.setRowCount(len(cases))
        for row_idx, tc in enumerate(cases):
            def cell(text, align=Qt.AlignLeft, mono=False):
                item = QTableWidgetItem(text)
                item.setTextAlignment(align | Qt.AlignVCenter)
                if mono:
                    item.setFont(QFont("Consolas", 11))
                    item.setForeground(QColor(styles.TEXT_SECONDARY))
                return item

            table.setItem(row_idx, 0, cell(tc["id"], mono=True))
            table.setItem(row_idx, 1, cell(tc["fault_type"]))
            table.setItem(row_idx, 2, cell(tc["variable"], mono=True))

            inj = cell("Yes", Qt.AlignCenter)
            inj.setForeground(QColor(styles.ACCENT_GREEN))
            table.setItem(row_idx, 3, inj)

            resp = cell(tc.get("system_response", ""), Qt.AlignLeft)
            resp.setForeground(QColor(styles.TEXT_SECONDARY))
            table.setItem(row_idx, 4, resp)

            res = cell(tc["outcome"], Qt.AlignCenter)
            _oc = {"Pass": styles.ACCENT_GREEN, "Fail": styles.ACCENT_RED}.get(
                tc["outcome"], styles.ACCENT_AMBER)
            res.setForeground(QColor(_oc))
            table.setItem(row_idx, 5, res)

            if tc["outcome"] == "Fail":
                for col in range(6):
                    item = table.item(row_idx, col)
                    if item:
                        item.setBackground(QColor("#2a0f0f"))

        tbl_outer.addWidget(table, stretch=1)
        vl.addWidget(tbl_card, stretch=1)

        scroll.setWidget(container)
        self._root.addWidget(scroll)

    def _clear(self):
        while self._root.count():
            item = self._root.takeAt(0)
            if item.widget():
                item.widget().deleteLater()