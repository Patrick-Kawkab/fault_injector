"""
tab_assistant.py
----------------
The Assistant tab: a chat UI over the framework assistant (Gemini via API).
Ask about features, how-to steps, the sensors/fault types/ISO rules, or the
latest campaign results. See chat_assistant.py for setup.
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QScrollArea,
    QFrame, QLineEdit, QPushButton,
)
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QFontMetrics

import styles
from chat_assistant import ChatWorker, format_results_context


class AssistantTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._history = []            # [{"role": ..., "content": ...}, ...]
        self._results_context = ""
        self._results_provider = None
        self._worker = None
        self._pending_lbl = None
        self._busy = False
        self._bubbles = []

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # --- scrollable message area ---
        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._scroll.setFrameShape(QFrame.NoFrame)
        self._scroll.setStyleSheet("background: transparent;")

        container = QWidget()
        container.setStyleSheet("background: transparent;")
        self._msgs = QVBoxLayout(container)
        self._msgs.setContentsMargins(16, 16, 16, 16)
        self._msgs.setSpacing(10)
        self._msgs.addStretch()       # keeps bubbles stacked at the top
        self._scroll.setWidget(container)
        root.addWidget(self._scroll, stretch=1)

        # --- input row ---
        input_row = QWidget()
        input_row.setStyleSheet(
            f"background: {styles.BG_PRIMARY}; "
            f"border-top: 1px solid {styles.BORDER_COLOR};"
        )
        hl = QHBoxLayout(input_row)
        hl.setContentsMargins(12, 10, 12, 10)
        hl.setSpacing(8)

        self._input = QLineEdit()
        self._input.setPlaceholderText(
            "Ask about the framework - features, how-to steps, the last results..."
        )
        self._input.setStyleSheet(
            f"QLineEdit {{ background: {styles.BG_CARD}; color: {styles.TEXT_PRIMARY}; "
            f"border: 1px solid {styles.BORDER_COLOR}; border-radius: 8px; "
            f"padding: 9px 12px; font-size: 13px; }}"
        )
        self._input.returnPressed.connect(self._send)

        self._send_btn = QPushButton("Send")
        self._send_btn.setStyleSheet(styles.BTN_PRIMARY)
        self._send_btn.setCursor(Qt.PointingHandCursor)
        self._send_btn.clicked.connect(self._send)

        hl.addWidget(self._input, stretch=1)
        hl.addWidget(self._send_btn)
        root.addWidget(input_row)

        # welcome message
        self._add_bubble(
            "Hi! I'm the framework assistant. Ask me how to do something "
            "(e.g. \"how do I run a campaign?\"), what a feature or sensor does, "
            "or about your latest results.",
            "assistant",
        )

    # ---- public: called by the main window when a campaign finishes ----
    def set_results_context(self, data: dict):
        try:
            self._results_context = format_results_context(data)
        except Exception:
            pass

    def set_results_provider(self, fn):
        """fn() returns the latest campaign payload (or None). The assistant calls
        it at send-time so it always has the most recent run, even if the per-finish
        push was ever missed."""
        self._results_provider = fn

    # ---- bubbles ----
    def _add_bubble(self, text, role):
        row = QHBoxLayout()
        row.setContentsMargins(0, 0, 0, 0)

        lbl = QLabel(text)
        lbl.setWordWrap(True)
        lbl.setTextInteractionFlags(Qt.TextSelectableByMouse)
        f = lbl.font(); f.setPixelSize(13); lbl.setFont(f)

        if role == "user":
            lbl.setStyleSheet(
                f"background: {styles.ACCENT_BLUE}; color: white; "
                f"border-radius: 10px; padding: 9px 12px; font-size: 13px;"
            )
            row.addStretch()
            row.addWidget(lbl)
        else:
            lbl.setStyleSheet(
                f"background: {styles.BG_CARD}; color: {styles.TEXT_PRIMARY}; "
                f"border: 1px solid {styles.BORDER_COLOR}; border-radius: 10px; "
                f"padding: 9px 12px; font-size: 13px;"
            )
            row.addWidget(lbl)
            row.addStretch()

        self._bubbles.append(lbl)
        self._size_bubble(lbl)

        # insert just before the trailing stretch
        self._msgs.insertLayout(self._msgs.count() - 1, row)
        self._scroll_to_bottom()
        return lbl

    # ---- bubble width: hug content, but cap at ~67% so long text wraps ----
    def _cap(self):
        vw = self._scroll.viewport().width()
        if vw <= 0:
            vw = self.width() or 800
        return max(220, min(int(vw * 0.67), 1050))

    def _size_bubble(self, lbl):
        cap = self._cap()
        fm = QFontMetrics(lbl.font())
        chrome = 28  # horizontal padding (12+12) + border
        inner = max(40, cap - chrome)
        rect = fm.boundingRect(0, 0, inner, 100000, Qt.TextWordWrap, lbl.text())
        lbl.setFixedWidth(min(rect.width() + chrome, cap))

    def resizeEvent(self, event):
        super().resizeEvent(event)
        for lbl in self._bubbles:
            self._size_bubble(lbl)

    def showEvent(self, event):
        super().showEvent(event)
        for lbl in self._bubbles:
            self._size_bubble(lbl)

    def _scroll_to_bottom(self):
        QTimer.singleShot(0, lambda: self._scroll.verticalScrollBar().setValue(
            self._scroll.verticalScrollBar().maximum()))

    # ---- send / receive ----
    def _send(self):
        text = self._input.text().strip()
        if not text or self._busy:
            return
        self._busy = True
        self._input.clear()

        self._add_bubble(text, "user")
        self._history.append({"role": "user", "content": text})

        self._pending_lbl = self._add_bubble("Thinking...", "assistant")
        self._input.setEnabled(False)
        self._send_btn.setEnabled(False)

        # pull the freshest campaign results so the bot always knows the last run
        if self._results_provider is not None:
            try:
                data = self._results_provider()
                if data:
                    self._results_context = format_results_context(data)
            except Exception:
                pass

        self._worker = ChatWorker(list(self._history), self._results_context)
        self._worker.reply_ready.connect(self._on_reply)
        self._worker.error.connect(self._on_error)
        self._worker.finished.connect(self._cleanup_worker)
        self._worker.start()

    def _on_reply(self, text):
        if self._pending_lbl is not None:
            self._pending_lbl.setText(text)
            self._size_bubble(self._pending_lbl)
            self._scroll_to_bottom()
        self._history.append({"role": "assistant", "content": text})
        self._reenable()

    def _on_error(self, msg):
        if self._pending_lbl is not None:
            self._pending_lbl.setText("\u26a0 " + msg)   # warning sign
            self._size_bubble(self._pending_lbl)
            self._scroll_to_bottom()
        # don't store errors in the conversation history
        self._reenable()

    def _reenable(self):
        self._pending_lbl = None
        self._busy = False
        self._input.setEnabled(True)
        self._send_btn.setEnabled(True)
        self._input.setFocus()

    def _cleanup_worker(self):
        self._worker = None