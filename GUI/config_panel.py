"""
config_panel.py
---------------
Left panel of the GUI — manual fault configuration form.
Emits a FaultConfig object upward when the user is ready to run.
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel,
    QLineEdit, QComboBox, QSpinBox, QPushButton,
    QScrollArea, QFrame, QSizePolicy, QStackedWidget,
    QCheckBox, QListWidget, QListWidgetItem
)
from PyQt5.QtCore import Qt, pyqtSignal

from config import FaultConfig, SENSOR_DB, FAULT_TYPES, ASIL_LEVELS, HARDWARE_MODES, GDB_PORTS, MACHINES, CPUS
import styles
from widgets import SectionLabel, FieldLabel, AITag, EmptyState, HDivider


class ConfigPanel(QWidget):
    """
    Left panel containing:
      - Header
      - Stacked widget: empty state OR filled form
      - Run button (disabled until form is valid)

    Signals:
      run_requested(FaultConfig)  — emitted when user clicks Run
    """
    run_requested = pyqtSignal(object)   # carries a FaultConfig

    def __init__(self, parent=None):
        super().__init__(parent)
        self._fault_list = []          # varied-fault campaign: list of FaultConfig
        self.setFixedWidth(290)
        self.setStyleSheet(f"background: {styles.BG_SECONDARY};")

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # ── Header ──
        header = QWidget()
        header.setFixedHeight(42)
        header.setStyleSheet(f"""
            background: {styles.BG_PRIMARY};
            border-bottom: 1px solid {styles.BORDER_COLOR};
        """)
        hl = QHBoxLayout(header)
        hl.setContentsMargins(14, 0, 14, 0)
        htitle = QLabel("⚙  TEST CONFIGURATION")
        htitle.setStyleSheet(f"color: {styles.TEXT_SECONDARY}; font-size: 11px; font-weight: bold; letter-spacing: 1px;")
        hl.addWidget(htitle)

        # ── Run button (must exist before building form page) ──
        self._run_btn = QPushButton("▶   Run Injection")
        self._run_btn.setFixedHeight(44)
        self._run_btn.setEnabled(False)
        self._run_btn.setStyleSheet(self._run_style(enabled=False))
        self._run_btn.clicked.connect(self._on_run)

        # ── Stacked widget (built AFTER run button exists) ──
        self._stack = QStackedWidget()
        self._empty_page = self._build_empty_page()
        self._form_page  = self._build_form_page()
        self._stack.addWidget(self._empty_page)
        self._stack.addWidget(self._form_page)

        run_wrap = QWidget()
        run_wrap.setStyleSheet(f"background: {styles.BG_SECONDARY}; border-top: 1px solid {styles.BORDER_COLOR};")
        rwl = QVBoxLayout(run_wrap)
        rwl.setContentsMargins(14, 10, 14, 10)
        rwl.addWidget(self._run_btn)

        root.addWidget(header)
        root.addWidget(self._stack, stretch=1)
        root.addWidget(run_wrap)

        # Start on empty page
        self._show_empty()

    # ── Empty page ────────────────────────────────────────────────────────────
    def _build_empty_page(self) -> QWidget:
        page = QWidget()
        page.setStyleSheet(f"background: {styles.BG_SECONDARY};")
        outer = QVBoxLayout(page)
        outer.setContentsMargins(0, 0, 0, 0)

        # Scroll so manual fields are accessible
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setStyleSheet("background: transparent;")

        inner = QWidget()
        inner.setStyleSheet("background: transparent;")
        vl = QVBoxLayout(inner)
        vl.setContentsMargins(14, 14, 14, 14)
        vl.setSpacing(6)

        # Guidance message
        msg_card = QFrame()
        msg_card.setStyleSheet(f"""
            background: #1a2a40;
            border: 1px solid {styles.BORDER_COLOR};
            border-radius: 8px;
        """)
        ml = QVBoxLayout(msg_card)
        ml.setContentsMargins(12, 12, 12, 12)
        ml.setSpacing(6)

        icon_lbl = QLabel("🪄")
        icon_lbl.setAlignment(Qt.AlignCenter)
        icon_lbl.setStyleSheet("font-size: 28px; background: transparent; border: none;")

        title_lbl = QLabel("No parameters yet")
        title_lbl.setAlignment(Qt.AlignCenter)
        title_lbl.setStyleSheet(f"font-size: 13px; font-weight: bold; color: {styles.TEXT_SECONDARY}; background: transparent; border: none;")

        sub_lbl = QLabel("Fill in the form below to configure\nyour fault injection test manually.")
        sub_lbl.setAlignment(Qt.AlignCenter)
        sub_lbl.setWordWrap(True)
        sub_lbl.setStyleSheet(f"font-size: 12px; color: {styles.TEXT_DISABLED}; background: transparent; border: none;")

        ml.addWidget(icon_lbl)
        ml.addWidget(title_lbl)
        ml.addWidget(sub_lbl)

        # Divider
        div_row = QHBoxLayout()
        div_row.setSpacing(8)
        line1 = QFrame(); line1.setFrameShape(QFrame.HLine)
        line1.setStyleSheet(f"color: {styles.BORDER_COLOR};")
        or_lbl = QLabel("fill in manually")
        or_lbl.setStyleSheet(f"color: {styles.TEXT_DISABLED}; font-size: 10px; background: transparent;")
        line2 = QFrame(); line2.setFrameShape(QFrame.HLine)
        line2.setStyleSheet(f"color: {styles.BORDER_COLOR};")
        div_row.addWidget(line1)
        div_row.addWidget(or_lbl)
        div_row.addWidget(line2)

        # Quick manual fields (subset shown on empty page)
        self._e_sensor   = self._make_combo(list(SENSOR_DB.keys()), "Select sensor...")
        self._e_fault    = self._make_combo(list(FAULT_TYPES.values()), "Select fault type...")
        self._e_variable = self._make_input("e.g. speed_RPM")
        self._e_duration = self._make_spin(1, 3600, 60, "s")

        self._e_sensor.currentIndexChanged.connect(self._on_empty_sensor_change)
        self._e_fault.currentIndexChanged.connect(self._check_empty_ready)
        self._e_variable.textChanged.connect(self._check_empty_ready)

        fill_btn = QPushButton("Continue to full form →")
        fill_btn.setStyleSheet(styles.BTN_PRIMARY)
        fill_btn.setFixedHeight(36)
        fill_btn.clicked.connect(self._populate_form_from_empty)

        vl.addWidget(msg_card)
        vl.addSpacing(8)
        vl.addLayout(div_row)
        vl.addSpacing(6)
        vl.addWidget(FieldLabel("Sensor"))
        vl.addWidget(self._e_sensor)
        vl.addWidget(FieldLabel("Fault type"))
        vl.addWidget(self._e_fault)
        vl.addWidget(FieldLabel("Variable"))
        vl.addWidget(self._e_variable)
        vl.addWidget(FieldLabel("Duration (s)"))
        vl.addWidget(self._e_duration)
        vl.addSpacing(8)
        vl.addWidget(fill_btn)
        vl.addStretch()

        scroll.setWidget(inner)
        outer.addWidget(scroll)
        return page

    # ── Full form page ────────────────────────────────────────────────────────
    def _build_form_page(self) -> QWidget:
        page = QWidget()
        page.setStyleSheet(f"background: {styles.BG_SECONDARY};")
        outer = QVBoxLayout(page)
        outer.setContentsMargins(0, 0, 0, 0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setStyleSheet("background: transparent;")

        inner = QWidget()
        inner.setStyleSheet("background: transparent;")
        vl = QVBoxLayout(inner)
        vl.setContentsMargins(14, 10, 14, 14)
        vl.setSpacing(6)

        # Clear/reset button
        reset_btn = QPushButton("↺  Clear & reset")
        reset_btn.setStyleSheet(styles.BTN_GHOST)
        reset_btn.setFixedHeight(28)
        reset_btn.clicked.connect(self._show_empty)

        # ── Hardware section ──
        vl.addWidget(reset_btn)
        vl.addWidget(SectionLabel("Hardware"))
        self._hw_combo = self._make_combo(list(HARDWARE_MODES.values()))
        vl.addWidget(FieldLabel("Target hardware"))
        vl.addWidget(self._hw_combo)

        # ── Fault parameters section ──
        vl.addWidget(SectionLabel("Fault parameters"))

        self._sensor_combo = self._make_combo(list(SENSOR_DB.keys()))
        self._sensor_combo.currentIndexChanged.connect(self._on_sensor_change)
        vl.addWidget(FieldLabel("Sensor"))
        vl.addWidget(self._sensor_combo)

        self._sensor_name_lbl = QLabel("")
        self._sensor_name_lbl.setStyleSheet(f"color: {styles.TEXT_DISABLED}; font-size: 11px; font-style: italic; background: transparent;")
        vl.addWidget(self._sensor_name_lbl)

        self._fault_combo = self._make_combo(list(FAULT_TYPES.values()))
        self._fault_combo.currentIndexChanged.connect(self._on_fault_type_change)
        vl.addWidget(FieldLabel("Fault type"))
        vl.addWidget(self._fault_combo)

        self._variable_label = FieldLabel("Variable")
        self._variable_input = self._make_input("e.g. speed_RPM")
        self._variable_input.textChanged.connect(self._check_run_ready)
        vl.addWidget(self._variable_label)
        vl.addWidget(self._variable_input)

        self._address_label = FieldLabel("Address (program counter)")
        self._address_input = self._make_input("e.g. 0x00000400")
        self._address_input.textChanged.connect(self._check_run_ready)
        vl.addWidget(self._address_label)
        vl.addWidget(self._address_input)

        # Min / Max row
        minmax_row = QHBoxLayout()
        minmax_row.setSpacing(8)
        self._min_spin = self._make_spin(-9999, 99999, 0)
        self._max_spin = self._make_spin(-9999, 99999, 255)
        min_col = QVBoxLayout()
        min_col.addWidget(FieldLabel("Min value"))
        min_col.addWidget(self._min_spin)
        max_col = QVBoxLayout()
        max_col.addWidget(FieldLabel("Max value"))
        max_col.addWidget(self._max_spin)
        minmax_row.addLayout(min_col)
        minmax_row.addLayout(max_col)
        vl.addLayout(minmax_row)

        # Fault value / bit position (context-sensitive)
        self._fault_val_label = FieldLabel("Fault value")
        self._fault_val_spin  = self._make_spin(-9999, 99999, 0)
        vl.addWidget(self._fault_val_label)
        vl.addWidget(self._fault_val_spin)

        self._bit_pos_label = FieldLabel("Bit position (0–31)")
        self._bit_pos_spin  = self._make_spin(0, 31, 0)
        vl.addWidget(self._bit_pos_label)
        vl.addWidget(self._bit_pos_spin)

        # System state — variable to monitor for safety (stuck faults only)
        self._system_state_label = FieldLabel("System state (variable to monitor)")
        self._system_state_input = self._make_input("e.g. cruise_active")
        vl.addWidget(self._system_state_label)
        vl.addWidget(self._system_state_input)

        # ASIL
        self._asil_combo = self._make_combo(ASIL_LEVELS)
        self._asil_combo.setCurrentText("ASIL-D")
        vl.addWidget(FieldLabel("ASIL level"))
        vl.addWidget(self._asil_combo)

        # ── Target / debug section (goes into the JSON meta) ──
        vl.addWidget(SectionLabel("Target / debug"))
        self._machine_combo = self._make_combo(MACHINES)
        self._cpu_combo     = self._make_combo(CPUS)
        self._gdb_combo     = self._make_combo([str(p) for p in GDB_PORTS])
        vl.addWidget(FieldLabel("Machine"))
        vl.addWidget(self._machine_combo)
        vl.addWidget(FieldLabel("CPU"))
        vl.addWidget(self._cpu_combo)
        vl.addWidget(FieldLabel("GDB port"))
        vl.addWidget(self._gdb_combo)

        # ── Timing section ──
        vl.addWidget(SectionLabel("Timing"))
        self._duration_spin = self._make_spin(1, 3600, 60, "s")
        vl.addWidget(FieldLabel("Duration"))
        vl.addWidget(self._duration_spin)

        self._num_faults_spin = self._make_spin(1, 1000, 30)
        vl.addWidget(FieldLabel("Number of faults"))
        vl.addWidget(self._num_faults_spin)

        # ── Vary faults: build a list of distinct faults, repeated to N ──
        self._vary_check = QCheckBox("Vary faults (build a list)")
        self._vary_check.toggled.connect(self._on_vary_toggled)
        vl.addWidget(self._vary_check)

        self._add_fault_btn = QPushButton("Add current fault to list")
        self._add_fault_btn.clicked.connect(self._on_add_fault)
        vl.addWidget(self._add_fault_btn)

        self._fault_list_widget = QListWidget()
        self._fault_list_widget.setMaximumHeight(120)
        vl.addWidget(self._fault_list_widget)

        list_btn_row = QHBoxLayout()
        self._remove_fault_btn = QPushButton("Remove last")
        self._clear_faults_btn = QPushButton("Clear")
        self._remove_fault_btn.clicked.connect(self._on_remove_fault)
        self._clear_faults_btn.clicked.connect(self._on_clear_faults)
        list_btn_row.addWidget(self._remove_fault_btn)
        list_btn_row.addWidget(self._clear_faults_btn)
        vl.addLayout(list_btn_row)

        self._vary_widgets = [self._add_fault_btn, self._fault_list_widget,
                              self._remove_fault_btn, self._clear_faults_btn]
        for _w in self._vary_widgets:
            _w.setVisible(False)

        # ── Expected behavior ──
        vl.addWidget(SectionLabel("Expected behavior"))
        self._expected_input = self._make_input("e.g. Cruise control disengages (leave blank to auto-fill from sensor)")
        vl.addWidget(self._expected_input)

        vl.addStretch()
        scroll.setWidget(inner)
        outer.addWidget(scroll)

        # Init visibility
        self._on_fault_type_change(0)
        return page

    # ── Helper: widget factories ──────────────────────────────────────────────
    def _make_input(self, placeholder: str = "") -> QLineEdit:
        w = QLineEdit()
        w.setPlaceholderText(placeholder)
        w.setFixedHeight(34)
        return w

    def _make_combo(self, items: list, placeholder: str = "") -> QComboBox:
        w = QComboBox()
        if placeholder:
            w.addItem(placeholder)
        w.addItems(items)
        w.setFixedHeight(34)
        # On Linux the popup is a separate top-level window that can escape the
        # app stylesheet, showing white text on a white background.
        # Styling the internal view directly fixes this.
        w.view().setStyleSheet(f"""
            QAbstractItemView {{
                background: {styles.BG_CARD};
                color: {styles.TEXT_PRIMARY};
                border: 1px solid {styles.BORDER_COLOR};
                selection-background-color: {styles.ACCENT_BLUE};
                selection-color: {styles.TEXT_PRIMARY};
                padding: 4px;
                outline: 0px;
            }}
            QAbstractItemView::item {{
                padding: 6px 10px;
                min-height: 24px;
                color: {styles.TEXT_PRIMARY};
                background: {styles.BG_CARD};
            }}
            QAbstractItemView::item:hover {{
                background: {styles.BG_INPUT};
                color: {styles.TEXT_PRIMARY};
            }}
            QAbstractItemView::item:selected {{
                background: {styles.ACCENT_BLUE};
                color: {styles.TEXT_PRIMARY};
            }}
        """)
        return w

    def _make_spin(self, mn: int, mx: int, default: int, suffix: str = "") -> QSpinBox:
        w = QSpinBox()
        w.setRange(mn, mx)
        w.setValue(default)
        w.setFixedHeight(34)
        if suffix:
            w.setSuffix(f"  {suffix}")
        return w

    @staticmethod
    def _run_style(enabled: bool) -> str:
        if enabled:
            return f"""
                QPushButton {{
                    background: {styles.ACCENT_RED};
                    color: white;
                    border: none;
                    border-radius: 6px;
                    font-size: 13px;
                    font-weight: bold;
                }}
                QPushButton:hover {{ background: #f05a4a; }}
                QPushButton:pressed {{ background: #c0392b; }}
            """
        return f"""
            QPushButton {{
                background: {styles.BG_CARD};
                color: {styles.TEXT_DISABLED};
                border: 1px solid {styles.BORDER_COLOR};
                border-radius: 6px;
                font-size: 13px;
                font-weight: bold;
            }}
        """

    # ── Slot: sensor selection (empty page) ───────────────────────────────────
    def _on_empty_sensor_change(self, idx: int):
        # Variable name comes from the hardware doc, so it isn't auto-filled.
        self._check_empty_ready()

    def _check_empty_ready(self):
        sensor_ok  = self._e_sensor.currentIndex() > 0
        fault_ok   = self._e_fault.currentIndex() > 0
        variable_ok = bool(self._e_variable.text().strip())
        can_continue = sensor_ok and fault_ok and variable_ok
        # Enable run directly if basics filled
        self._run_btn.setEnabled(can_continue)
        self._run_btn.setStyleSheet(self._run_style(can_continue))

    # ── Slot: sensor selection (full form) ────────────────────────────────────
    def _on_sensor_change(self, idx: int):
        key = self._sensor_combo.currentText()
        if key in SENSOR_DB:
            db = SENSOR_DB[key]
            self._sensor_name_lbl.setText(db["name"])
            self._min_spin.setValue(db["min_value"])
            self._max_spin.setValue(db["max_value"])
            self._asil_combo.setCurrentText(db["asil_level"])
        self._check_run_ready()

    # ── Slot: fault type selection ────────────────────────────────────────────
    def _on_fault_type_change(self, idx: int):
        fault_key = list(FAULT_TYPES.keys())[max(idx, 0)]
        is_bit_flip = (fault_key == "bit_flip")
        is_pc       = (fault_key == "pc_error")
        is_task     = (fault_key == "task_delay")
        has_state   = fault_key in ("sensor_corruption", "task_delay")
        shows_value = fault_key in ("sensor_corruption", "memory_corruption", "task_delay")

        # Variable vs Address: PC error targets the program counter (an address).
        self._variable_label.setVisible(not is_pc)
        self._variable_input.setVisible(not is_pc)
        self._address_label.setVisible(is_pc)
        self._address_input.setVisible(is_pc)

        # Value field — relabelled to the delay magnitude for Task Delay.
        self._fault_val_label.setText("Delay magnitude (ms)" if is_task else "Fault value")
        self._fault_val_label.setVisible(shows_value)
        self._fault_val_spin.setVisible(shows_value)

        self._bit_pos_label.setVisible(is_bit_flip)
        self._bit_pos_spin.setVisible(is_bit_flip)

        self._system_state_label.setVisible(has_state)
        self._system_state_input.setVisible(has_state)

        self._check_run_ready()

    # ── Slot: validate and enable run ─────────────────────────────────────────
    def _check_run_ready(self):
        sensor_ok = self._sensor_combo.currentText() in SENSOR_DB
        fault_key = list(FAULT_TYPES.keys())[max(self._fault_combo.currentIndex(), 0)]
        if fault_key == "pc_error":
            loc_ok = bool(self._address_input.text().strip())
        else:
            loc_ok = bool(self._variable_input.text().strip())
        ready = sensor_ok and loc_ok
        self._run_btn.setEnabled(ready)
        self._run_btn.setStyleSheet(self._run_style(ready))

    # ── State transitions ─────────────────────────────────────────────────────
    def _show_empty(self):
        self._stack.setCurrentIndex(0)
        self._run_btn.setEnabled(False)
        self._run_btn.setStyleSheet(self._run_style(False))

    def _populate_form_from_empty(self):
        """Copy values from empty-page quick fields into the full form."""
        sensor_text = self._e_sensor.currentText()
        if sensor_text in SENSOR_DB:
            self._sensor_combo.setCurrentText(sensor_text)
        fault_text = self._e_fault.currentText()
        for key, val in FAULT_TYPES.items():
            if val == fault_text:
                self._fault_combo.setCurrentText(val)
                break
        var = self._e_variable.text().strip()
        if var:
            self._variable_input.setText(var)
        self._duration_spin.setValue(self._e_duration.value())
        self._stack.setCurrentIndex(1)
        self._check_run_ready()

    # ── Build FaultConfig from form ───────────────────────────────────────────
    def _fault_summary(self, c) -> str:
        loc = c.variable or c.address
        label = FAULT_TYPES.get(c.fault_type, c.fault_type)
        if c.fault_type in ("sensor_corruption", "memory_corruption"):
            extra = f" = {c.fault_value}"
        elif c.fault_type == "task_delay":
            extra = f" delay {c.fault_value}ms"
        elif c.fault_type == "bit_flip":
            extra = f" bit {c.bit_position}"
        else:
            extra = ""
        return f"{label} · {loc}{extra}"

    def _on_vary_toggled(self, checked: bool):
        for w in self._vary_widgets:
            w.setVisible(checked)

    def _on_add_fault(self):
        cfg = self._build_config()
        ok, err = cfg.is_valid()
        if not ok:
            from PyQt5.QtWidgets import QMessageBox
            QMessageBox.warning(self, "Incomplete fault", err)
            return
        self._fault_list.append(cfg)
        self._fault_list_widget.addItem(
            QListWidgetItem(f"{len(self._fault_list)}.  {self._fault_summary(cfg)}"))

    def _on_remove_fault(self):
        if self._fault_list:
            self._fault_list.pop()
            self._fault_list_widget.takeItem(self._fault_list_widget.count() - 1)

    def _on_clear_faults(self):
        self._fault_list.clear()
        self._fault_list_widget.clear()

    def _build_config(self) -> FaultConfig:
        hw_text   = self._hw_combo.currentText()
        hw_key    = "tivac" if "Tiva" in hw_text else "qemu"
        sensor    = self._sensor_combo.currentText()
        fault_idx = self._fault_combo.currentIndex()
        fault_key = list(FAULT_TYPES.keys())[fault_idx]
        is_pc     = fault_key == "pc_error"
        has_state = fault_key in ("sensor_corruption", "task_delay")

        return FaultConfig(
            hardware         = hw_key,
            sensor           = sensor,
            fault_type       = fault_key,
            variable         = "" if is_pc else self._variable_input.text().strip(),
            address          = self._address_input.text().strip() if is_pc else "",
            system_state     = self._system_state_input.text().strip() if has_state else "",
            duration_s       = self._duration_spin.value(),
            min_value        = self._min_spin.value(),
            max_value        = self._max_spin.value(),
            fault_value      = self._fault_val_spin.value(),
            bit_position     = self._bit_pos_spin.value(),
            asil_level       = self._asil_combo.currentText(),
            machine          = self._machine_combo.currentText(),
            cpu              = self._cpu_combo.currentText(),
            gdb_port         = int(self._gdb_combo.currentText()),
            num_faults       = self._num_faults_spin.value(),
            expected_behavior= self._expected_input.text().strip(),
        )

    # ── Run clicked ───────────────────────────────────────────────────────────
    def _on_run(self):
        if self._stack.currentIndex() == 0:
            # Build minimal config from empty page fields
            sensor_text = self._e_sensor.currentText()
            hw_key = "tivac"
            fault_idx = max(self._e_fault.currentIndex() - 1, 0)
            fault_key = list(FAULT_TYPES.keys())[fault_idx]
            db = SENSOR_DB.get(sensor_text, {})
            is_pc = fault_key == "pc_error"
            loc = self._e_variable.text().strip()
            cfg = FaultConfig(
                hardware   = hw_key,
                sensor     = sensor_text,
                fault_type = fault_key,
                variable   = "" if is_pc else loc,
                address    = loc if is_pc else "",
                duration_s = self._e_duration.value(),
                min_value  = db.get("min_value", 0),
                max_value  = db.get("max_value", 255),
                asil_level = db.get("asil_level", "ASIL-D"),
            )
        else:
            cfg = self._build_config()
            if self._vary_check.isChecked():
                if not self._fault_list:
                    from PyQt5.QtWidgets import QMessageBox
                    QMessageBox.warning(self, "No faults in list",
                        "Add at least one fault to the list, or uncheck 'Vary faults'.")
                    return
                # Distinct faults (each validated when added), repeated to N.
                cfg.varied_faults = [c.fault_payload() for c in self._fault_list]
                self.run_requested.emit(cfg)
                return

        valid, err = cfg.is_valid()
        if not valid:
            from PyQt5.QtWidgets import QMessageBox
            QMessageBox.warning(self, "Invalid configuration", err)
            return

        self.run_requested.emit(cfg)

    # ── Lock / Unlock (called by MainWindow) ──────────────────────────────────
    def lock(self):
        """
        Disable all inputs and the run button.
        Called after the user presses Run, until they click Edit or campaign finishes.
        """
        self._locked = True
        self._run_btn.setEnabled(False)
        self._run_btn.setStyleSheet(self._run_style(False))
        self._set_inputs_enabled(False)
        # Show a locked overlay hint in the header
        if hasattr(self, '_lock_lbl'):
            self._lock_lbl.show()

    def unlock(self):
        """
        Re-enable all inputs and the run button.
        Called when the user clicks Edit in the confirmation banner,
        or when the campaign finishes.
        """
        self._locked = False
        self._set_inputs_enabled(True)
        self._check_run_ready()
        if hasattr(self, '_lock_lbl'):
            self._lock_lbl.hide()

    def _set_inputs_enabled(self, enabled: bool):
        """Enable or disable all interactive widgets in both pages."""
        # Full form widgets
        for widget in [
            self._hw_combo, self._sensor_combo, self._fault_combo,
            self._variable_input, self._address_input, self._system_state_input, self._min_spin, self._max_spin,
            self._fault_val_spin, self._bit_pos_spin, self._asil_combo,
            self._machine_combo, self._cpu_combo, self._gdb_combo, self._num_faults_spin,
            self._vary_check, self._add_fault_btn, self._remove_fault_btn, self._clear_faults_btn,
            self._duration_spin, self._expected_input,
        ]:
            widget.setEnabled(enabled)

        # Empty page quick fields
        for widget in [
            self._e_sensor, self._e_fault, self._e_variable, self._e_duration,
        ]:
            widget.setEnabled(enabled)

        # Style tweak: dim everything when locked
        opacity = "1.0" if enabled else "0.45"
        self._stack.setStyleSheet(
            f"background: transparent;" if enabled
            else f"background: transparent; opacity: {opacity};"
        )
