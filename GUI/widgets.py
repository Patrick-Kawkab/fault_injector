"""
widgets.py
----------
Reusable custom widgets used across the Fault Injection Framework GUI.
"""

from PyQt5.QtWidgets import (
    QWidget, QLabel, QHBoxLayout, QVBoxLayout,
    QPushButton, QFrame, QSizePolicy
)
from PyQt5.QtCore import Qt, QPropertyAnimation, QEasingCurve, pyqtProperty
from PyQt5.QtGui import QFont, QColor, QPainter, QPen, QBrush
import styles


# ── Section label ─────────────────────────────────────────────────────────────
class SectionLabel(QLabel):
    """Small uppercase label used as a section divider."""
    def __init__(self, text: str, parent=None):
        super().__init__(text.upper(), parent)
        self.setStyleSheet(f"""
            color: {styles.TEXT_DISABLED};
            font-size: 10px;
            font-weight: bold;
            letter-spacing: 1.5px;
            padding-top: 8px;
            padding-bottom: 2px;
        """)


# ── Field label ───────────────────────────────────────────────────────────────
class FieldLabel(QLabel):
    """Label above an input field."""
    def __init__(self, text: str, parent=None):
        super().__init__(text, parent)
        self.setStyleSheet(f"""
            color: {styles.TEXT_SECONDARY};
            font-size: 12px;
            font-weight: bold;
            padding-bottom: 2px;
        """)


# ── AI tag badge ──────────────────────────────────────────────────────────────
class AITag(QLabel):
    """Small 'AI' badge shown next to auto-filled fields."""
    def __init__(self, parent=None):
        super().__init__("AI", parent)
        self.setFixedSize(28, 18)
        self.setAlignment(Qt.AlignCenter)
        self.setStyleSheet(f"""
            background: #1a2a40;
            color: {styles.ACCENT_BLUE};
            border: 1px solid {styles.ACCENT_BLUE};
            border-radius: 9px;
            font-size: 9px;
            font-weight: bold;
            letter-spacing: 0.5px;
        """)


# ── Status badge ──────────────────────────────────────────────────────────────
class StatusBadge(QLabel):
    COLORS = {
        "ok":      ("#1d9e75", "#0d3028"),
        "fail":    ("#e74c3c", "#3d1010"),
        "warn":    ("#f0a500", "#3d2a00"),
        "info":    ("#4a9eff", "#1a2a40"),
        "idle":    ("#9e9e9e", "#1a1a2e"),
    }

    def __init__(self, text: str, status: str = "idle", parent=None):
        super().__init__(text, parent)
        self.setStatus(status)
        self.setAlignment(Qt.AlignCenter)

    def setStatus(self, status: str):
        fg, bg = self.COLORS.get(status, self.COLORS["idle"])
        self.setStyleSheet(f"""
            color: {fg};
            background: {bg};
            border: 1px solid {fg};
            border-radius: 10px;
            padding: 2px 10px;
            font-size: 11px;
            font-weight: bold;
        """)


# ── Metric card ───────────────────────────────────────────────────────────────
class MetricCard(QWidget):
    """A small card showing a label + big value + sub-label."""
    def __init__(self, label: str, value: str, sub: str = "",
                 color: str = styles.TEXT_PRIMARY, parent=None):
        super().__init__(parent)
        self.setStyleSheet(f"""
            QWidget {{
                background: {styles.BG_CARD};
                border: 1px solid {styles.BORDER_COLOR};
                border-radius: 8px;
            }}
        """)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(14, 12, 14, 12)
        layout.setSpacing(3)

        self._label_w = QLabel(label)
        self._label_w.setStyleSheet(f"color: {styles.TEXT_SECONDARY}; font-size: 11px; background: transparent; border: none;")

        self._value_w = QLabel(value)
        self._value_w.setStyleSheet(f"color: {color}; font-size: 22px; font-weight: bold; background: transparent; border: none;")

        self._sub_w = QLabel(sub)
        self._sub_w.setStyleSheet(f"color: {styles.TEXT_DISABLED}; font-size: 10px; background: transparent; border: none;")

        layout.addWidget(self._label_w)
        layout.addWidget(self._value_w)
        if sub:
            layout.addWidget(self._sub_w)

    def setValue(self, value: str, color: str = None):
        self._value_w.setText(value)
        if color:
            self._value_w.setStyleSheet(f"color: {color}; font-size: 22px; font-weight: bold; background: transparent; border: none;")

    def setSub(self, sub: str):
        self._sub_w.setText(sub)


# ── Card frame ────────────────────────────────────────────────────────────────
class CardFrame(QFrame):
    """Generic card container with dark background and border."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setStyleSheet(f"""
            QFrame {{
                background: {styles.BG_SECONDARY};
                border: 1px solid {styles.BORDER_COLOR};
                border-radius: 8px;
            }}
        """)
        self._layout = QVBoxLayout(self)
        self._layout.setContentsMargins(14, 12, 14, 12)
        self._layout.setSpacing(8)

    def layout(self):
        return self._layout

    def addWidget(self, w):
        self._layout.addWidget(w)

    def addLayout(self, l):
        self._layout.addLayout(l)


# ── Card title label ──────────────────────────────────────────────────────────
class CardTitle(QLabel):
    def __init__(self, text: str, parent=None):
        super().__init__(text, parent)
        self.setStyleSheet(f"""
            color: {styles.TEXT_SECONDARY};
            font-size: 14px;
            font-weight: bold;
            background: transparent;
            border: none;
            padding-bottom: 8px;
        """)


# ── Horizontal divider ────────────────────────────────────────────────────────
class HDivider(QFrame):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.HLine)
        self.setFixedHeight(1)
        self.setStyleSheet(f"background: {styles.BORDER_COLOR}; border: none;")


# ── Empty state widget ────────────────────────────────────────────────────────
class EmptyState(QWidget):
    """Shown in a tab or panel when there's no data yet."""
    def __init__(self, icon: str, title: str, subtitle: str, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignCenter)
        layout.setSpacing(10)

        icon_lbl = QLabel(icon)
        icon_lbl.setAlignment(Qt.AlignCenter)
        icon_lbl.setStyleSheet(f"font-size: 40px; color: {styles.TEXT_DISABLED}; background: transparent;")

        title_lbl = QLabel(title)
        title_lbl.setAlignment(Qt.AlignCenter)
        title_lbl.setStyleSheet(f"font-size: 14px; font-weight: bold; color: {styles.TEXT_SECONDARY}; background: transparent;")

        sub_lbl = QLabel(subtitle)
        sub_lbl.setAlignment(Qt.AlignCenter)
        sub_lbl.setWordWrap(True)
        sub_lbl.setMaximumWidth(300)
        sub_lbl.setStyleSheet(f"font-size: 12px; color: {styles.TEXT_DISABLED}; background: transparent;")

        layout.addWidget(icon_lbl)
        layout.addWidget(title_lbl)
        layout.addWidget(sub_lbl)


# ── Confirm banner ────────────────────────────────────────────────────────────
class ConfirmBanner(QWidget):
    """Warning banner shown after config is ready, asking user to confirm."""
    def __init__(self, message: str, on_confirm, on_edit, parent=None):
        super().__init__(parent)
        self.setStyleSheet(f"""
            QWidget {{
                background: #2a1f00;
                border: 1px solid {styles.ACCENT_AMBER};
                border-radius: 8px;
            }}
        """)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(14, 10, 14, 10)

        msg = QLabel(message)
        msg.setWordWrap(True)
        msg.setStyleSheet(f"color: {styles.ACCENT_AMBER}; font-size: 12px; background: transparent; border: none;")

        confirm_btn = QPushButton("✓  Confirm")
        confirm_btn.setStyleSheet(f"""
            QPushButton {{
                background: #0d3028;
                color: {styles.ACCENT_GREEN};
                border: 1px solid {styles.ACCENT_GREEN};
                border-radius: 5px;
                padding: 5px 14px;
                font-size: 12px;
                font-weight: bold;
            }}
            QPushButton:hover {{ background: #1d5040; }}
        """)
        confirm_btn.setFixedHeight(30)
        confirm_btn.clicked.connect(on_confirm)

        edit_btn = QPushButton("Edit")
        edit_btn.setStyleSheet(f"""
            QPushButton {{
                background: transparent;
                color: {styles.TEXT_SECONDARY};
                border: 1px solid {styles.BORDER_COLOR};
                border-radius: 5px;
                padding: 5px 12px;
                font-size: 12px;
            }}
            QPushButton:hover {{ background: {styles.BG_CARD}; }}
        """)
        edit_btn.setFixedHeight(30)
        edit_btn.clicked.connect(on_edit)

        layout.addWidget(msg, stretch=1)
        layout.addWidget(confirm_btn)
        layout.addWidget(edit_btn)