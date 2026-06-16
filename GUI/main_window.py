"""
main_window.py
--------------
Main application window for the Fault Injection Framework GUI.
Wires together: ConfigPanel ← → Tabs (Monitor, Results, Report)
                            ← → MockInjectionWorker (QThread)

Panels are separated by a QSplitter — drag the dividers to resize.
"""

import os

from PyQt5.QtWidgets import (
    QMainWindow, QWidget, QHBoxLayout, QVBoxLayout,
    QTabWidget, QLabel, QSplitter, QSizePolicy
)
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QFont

import styles
from config import FaultConfig
from config_panel import ConfigPanel
from tab_monitor import MonitorTab
from tab_results import ResultsTab
from tab_report  import ReportTab
from tab_assistant import AssistantTab
from orchestrator import OrchestratorWorker 


class TitleBar(QWidget):
    """Custom title bar at the top of the window."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedHeight(48)
        self.setStyleSheet(f"""
            background: {styles.BG_PRIMARY};
            border-bottom: 1px solid {styles.BORDER_COLOR};
        """)

        hl = QHBoxLayout(self)
        hl.setContentsMargins(10, 0, 10, 0)

        # Left: icon + title
        left = QHBoxLayout()
        left.setSpacing(10)
        cpu_icon = QLabel("💻")
        cpu_icon.setStyleSheet("font-size: 20px; background: transparent;")
        title_col = QVBoxLayout()
        title_col.setSpacing(0)
        title_lbl = QLabel("Fault Injection Framework")
        title_lbl.setStyleSheet(
            f"font-size: 14px; font-weight: bold; color: {styles.TEXT_PRIMARY}; background: transparent;"
        )
        sub_lbl = QLabel("Tiva-C  ·  ISO 26262  ·  ASIL-D")
        sub_lbl.setStyleSheet(
            f"font-size: 11px; color: {styles.TEXT_SECONDARY}; background: transparent;"
        )
        title_col.addWidget(title_lbl)
        title_col.addWidget(sub_lbl)
        left.addWidget(cpu_icon)
        left.addLayout(title_col)

        # Right: status badges
        right = QHBoxLayout()
        right.setSpacing(8)
        right.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self._hw_badge = self._make_badge("Hardware: disconnected", "idle")
        self._ai_badge = self._make_badge("AI agent: ready", "ok")
        right.addWidget(self._hw_badge)
        right.addWidget(self._ai_badge)

        hl.addLayout(left)
        hl.addStretch()
        hl.addLayout(right)

    def _make_badge(self, text: str, status: str) -> QLabel:
        colors = {
            "ok":   (styles.ACCENT_GREEN, "#0d3028"),
            "idle": (styles.TEXT_SECONDARY, styles.BG_CARD),
            "warn": (styles.ACCENT_AMBER, "#2a1f00"),
        }
        fg, bg = colors.get(status, colors["idle"])
        lbl = QLabel(text)
        lbl.setStyleSheet(f"""
            color: {fg};
            background: {bg};
            border: 1px solid {fg};
            border-radius: 10px;
            padding: 3px 12px;
            font-size: 11px;
            font-weight: bold;
        """)
        return lbl

    def set_hardware_status(self, connected: bool):
        fg = styles.ACCENT_GREEN if connected else styles.TEXT_SECONDARY
        bg = "#0d3028" if connected else styles.BG_CARD
        status = "connected" if connected else "disconnected"
        self._hw_badge.setText(f"Hardware: {status}")
        self._hw_badge.setStyleSheet(f"""
            color: {fg}; background: {bg};
            border: 1px solid {fg};
            border-radius: 10px; padding: 3px 12px;
            font-size: 11px; font-weight: bold;
        """)


class RightPanel(QWidget):
    """Right side: tab bar + three tabs."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setStyleSheet(f"background: {styles.BG_SECONDARY};")
        vl = QVBoxLayout(self)
        vl.setContentsMargins(0, 0, 0, 0)
        vl.setSpacing(0)

        self._tabs = QTabWidget()
        self._tabs.setDocumentMode(True)

        self.monitor_tab = MonitorTab()
        self.results_tab = ResultsTab()
        self.report_tab  = ReportTab()
        self.assistant_tab = AssistantTab()

        self._tabs.addTab(self.monitor_tab, "  📡  Live monitor  ")
        self._tabs.addTab(self.results_tab, "  📊  Results  ")
        self._tabs.addTab(self.report_tab,  "  📄  AI report  ")
        self._tabs.addTab(self.assistant_tab, "  💬  Assistant  ")

        self._meta_lbl = QLabel("No active campaign")
        self._meta_lbl.setStyleSheet(f"""
            color: {styles.TEXT_DISABLED};
            font-size: 11px;
            padding-right: 12px;
            background: transparent;
        """)
        self._tabs.setCornerWidget(self._meta_lbl, Qt.TopRightCorner)

        vl.addWidget(self._tabs)

    def set_meta(self, text: str):
        self._meta_lbl.setText(text)

    def switch_to(self, index: int):
        self._tabs.setCurrentIndex(index)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Fault Injection Framework")
        self.setMinimumSize(900, 620)
        self.resize(1280, 740)

        self._worker = None
        self._current_config: FaultConfig = None
        self._results_data: dict = None

        self._build_ui()

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # ── Title bar (fixed height, not part of splitter) ──
        self._title_bar = TitleBar()
        root.addWidget(self._title_bar)

        # ── Body row: config panel + right panel (sidebar removed) ──
        body_row = QHBoxLayout()
        body_row.setContentsMargins(0, 0, 0, 0)
        body_row.setSpacing(0)

        # Splitter holds config panel (left) and right panel (right)
        # The user can drag the handle between them freely
        self._splitter = QSplitter(Qt.Horizontal)
        self._splitter.setHandleWidth(4)
        self._splitter.setStyleSheet(f"""
            QSplitter::handle {{
                background: {styles.BORDER_COLOR};
                border-radius: 2px;
            }}
            QSplitter::handle:hover {{
                background: {styles.ACCENT_BLUE};
            }}
            QSplitter::handle:pressed {{
                background: {styles.ACCENT_BLUE};
            }}
        """)

        self._config_panel = ConfigPanel()
        self._right_panel  = RightPanel()

        # Remove the fixed width from config panel so splitter controls it
        self._config_panel.setMinimumWidth(220)
        self._config_panel.setMaximumWidth(600)
        self._config_panel.setFixedWidth(290)   # initial width only

        self._right_panel.setMinimumWidth(400)

        self._splitter.addWidget(self._config_panel)
        self._splitter.addWidget(self._right_panel)

        # Initial size ratio: config panel ~290px, rest goes to right panel
        self._splitter.setSizes([290, 900])

        # Make right panel stretch more aggressively when window resizes
        self._splitter.setStretchFactor(0, 0)   # config panel: don't auto-stretch
        self._splitter.setStretchFactor(1, 1)   # right panel: stretch to fill

        self._config_panel.run_requested.connect(self._on_run_requested)

        # Assistant pulls the latest finished campaign at send-time. _results_data is
        # set on the very first line of _on_finished, so this is robust.
        self._right_panel.assistant_tab.set_results_provider(
            lambda: getattr(self, "_results_data", None)
        )

        body_row.addWidget(self._splitter, stretch=1)
        root.addLayout(body_row, stretch=1)

        # Remove the fixed width constraint so splitter can control it
        self._config_panel.setFixedWidth(self._config_panel.width())  # unlock after set

        # ── Status bar ──
        sb = self.statusBar()
        sb.setStyleSheet(f"""
            QStatusBar {{
                background: {styles.BG_PRIMARY};
                color: {styles.TEXT_DISABLED};
                font-size: 11px;
                border-top: 1px solid {styles.BORDER_COLOR};
            }}
        """)
        sb.showMessage("Ready  ·  Configure a test on the left and press Run  ·  Drag the divider to resize panels")

    def resizeEvent(self, event):
        """Keep config panel from having a fixed width after first show."""
        super().resizeEvent(event)
        # Remove fixed width constraint once the window is shown
        if hasattr(self, '_config_panel'):
            self._config_panel.setMinimumWidth(220)
            self._config_panel.setMaximumWidth(600)

    # ── Slot: run button pressed ──────────────────────────────────────────────
    def _on_run_requested(self, config: FaultConfig):
        self._current_config = config

        # Lock the config panel so user can't edit while confirming
        self._config_panel.lock()

        self._right_panel.monitor_tab.show_running(
            config,
            on_confirm=self._on_confirmed,
            on_edit=self._on_edit,
        )
        self._right_panel.switch_to(0)

        from config import FAULT_TYPES
        fault_lbl = FAULT_TYPES.get(config.fault_type, config.fault_type)
        self._right_panel.set_meta(
            f"Campaign: {config.sensor}_{config.fault_type} · pending confirmation"
        )
        self.statusBar().showMessage(
            f"Config ready — {config.sensor} · {fault_lbl} · {config.variable or config.address} · "
            f"Confirm or edit in the monitor tab"
        )

    # ── Slot: user confirmed ──────────────────────────────────────────────────
    def _on_confirmed(self):
        if self._current_config is None:
            return

        self._right_panel.monitor_tab.hide_banner()
        self._title_bar.set_hardware_status(True)
        self.statusBar().showMessage("Injection running...")

        cfg = self._current_config
        self._right_panel.set_meta(
            f"Campaign: {cfg.sensor}_{cfg.fault_type} · running..."
        )

        self._worker = OrchestratorWorker(cfg, injector_binary=os.environ.get("FI_INJECTOR"))
        self._worker.log_line.connect(self._right_panel.monitor_tab.append_log)
        self._worker.progress.connect(self._right_panel.monitor_tab.set_progress)
        self._worker.finished.connect(self._on_finished)
        self._worker.error.connect(self._on_error)
        self._worker.start()

    # ── Slot: user clicked edit ───────────────────────────────────────────────
    def _on_edit(self):
        self._config_panel.unlock()
        self.statusBar().showMessage("Edit the configuration on the left, then press Run again.")

    # ── Slot: injection finished ──────────────────────────────────────────────
    def _on_finished(self, data: dict):
        self._results_data = data
        cfg      = data["config"]
        coverage = data["handling_pct"]
        handled  = data["handled"]
        total    = data["total"]
        passed   = data["passed_asil"]

        self._right_panel.monitor_tab.set_complete()
        self._right_panel.results_tab.show_results(data)
        self._right_panel.report_tab.show_report(data)
        self._right_panel.assistant_tab.set_results_context(data)

        self._right_panel.set_meta(
            f"Campaign: {cfg.sensor}_{cfg.fault_type} · "
            f"{handled}/{total} handled · {coverage}% handling rate"
        )
        self._config_panel.unlock()
        self._title_bar.set_hardware_status(False)
        status_icon = "✓" if passed else "✗"
        self.statusBar().showMessage(
            f"{status_icon}  Campaign complete  ·  {coverage}% fault-handling rate  ·  "
            f"{cfg.asil_level + ' requirement met' if passed else cfg.asil_level + ' requirement NOT met — review findings'}"
        )

    # ── Slot: injection error ─────────────────────────────────────────────────
    def _on_error(self, msg: str):
        self._right_panel.monitor_tab.append_log(f"[ERROR] {msg}")
        self.statusBar().showMessage(f"Error: {msg}")
        self._config_panel.unlock()
        self._title_bar.set_hardware_status(False)

    def closeEvent(self, event):
        if self._worker and self._worker.isRunning():
            self._worker.stop()
            self._worker.wait(2000)
        event.accept()