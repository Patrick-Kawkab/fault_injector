"""
styles.py
---------
All colors, fonts, and Qt stylesheets for the Fault Injection Framework GUI.
Edit here to restyle the entire app in one place.
"""

# ── Color palette ────────────────────────────────────────────────────────────
BG_PRIMARY   = "#1a1a2e"   # darkest — window / sidebar background
BG_SECONDARY = "#16213e"   # panels background
BG_CARD      = "#0f3460"   # card / elevated surface
BG_INPUT     = "#1e2a45"   # input fields

ACCENT_BLUE  = "#4a9eff"
ACCENT_GREEN = "#1d9e75"
ACCENT_AMBER = "#f0a500"
ACCENT_RED   = "#e74c3c"

TEXT_PRIMARY   = "#e8eaf6"
TEXT_SECONDARY = "#9e9e9e"
TEXT_DISABLED  = "#555577"

BORDER_COLOR   = "#2a2a4a"
BORDER_ACCENT  = "#4a9eff"

# ── Full app stylesheet ───────────────────────────────────────────────────────
APP_STYLE = f"""
/* ── Global ── */
QWidget {{
    background-color: {BG_PRIMARY};
    color: {TEXT_PRIMARY};
    font-family: 'Segoe UI', 'Inter', 'Arial', sans-serif;
    font-size: 13px;
}}

QMainWindow {{
    background-color: {BG_PRIMARY};
}}

/* ── Scroll bars ── */
QScrollBar:vertical {{
    background: {BG_PRIMARY};
    width: 6px;
    border-radius: 3px;
}}
QScrollBar::handle:vertical {{
    background: {BG_CARD};
    border-radius: 3px;
    min-height: 20px;
}}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
    height: 0px;
}}
QScrollBar:horizontal {{
    background: {BG_PRIMARY};
    height: 6px;
}}
QScrollBar::handle:horizontal {{
    background: {BG_CARD};
    border-radius: 3px;
}}

/* ── Tab widget ── */
QTabWidget::pane {{
    border: 1px solid {BORDER_COLOR};
    background: {BG_SECONDARY};
    border-radius: 6px;
}}
QTabBar::tab {{
    background: {BG_PRIMARY};
    color: {TEXT_SECONDARY};
    padding: 10px 20px;
    border: none;
    font-size: 13px;
    min-width: 120px;
}}
QTabBar::tab:selected {{
    color: {TEXT_PRIMARY};
    border-bottom: 2px solid {ACCENT_BLUE};
    background: {BG_SECONDARY};
    font-weight: bold;
}}
QTabBar::tab:hover:!selected {{
    color: {TEXT_PRIMARY};
    background: {BG_CARD};
}}

/* ── Group boxes ── */
QGroupBox {{
    border: 1px solid {BORDER_COLOR};
    border-radius: 6px;
    margin-top: 12px;
    padding: 10px;
    font-size: 11px;
    font-weight: bold;
    color: {TEXT_SECONDARY};
    text-transform: uppercase;
    letter-spacing: 1px;
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    padding: 0 6px;
    background: {BG_SECONDARY};
}}

/* ── Labels ── */
QLabel {{
    color: {TEXT_PRIMARY};
    background: transparent;
}}

/* ── Line edits / inputs ── */
QLineEdit {{
    background: {BG_INPUT};
    border: 1px solid {BORDER_COLOR};
    border-radius: 5px;
    padding: 7px 10px;
    color: {TEXT_PRIMARY};
    font-size: 13px;
    selection-background-color: {ACCENT_BLUE};
}}
QLineEdit:focus {{
    border: 1px solid {ACCENT_BLUE};
}}
QLineEdit:disabled {{
    color: {TEXT_DISABLED};
    background: {BG_PRIMARY};
}}
QLineEdit[filled="true"] {{
    border: 1px solid {ACCENT_BLUE};
    background: #1a2a40;
    color: {ACCENT_BLUE};
}}

/* ── Combo boxes ── */
QComboBox {{
    background: {BG_INPUT};
    border: 1px solid {BORDER_COLOR};
    border-radius: 5px;
    padding: 7px 10px;
    color: {TEXT_PRIMARY};
    font-size: 13px;
    min-width: 120px;
}}
QComboBox:focus {{
    border: 1px solid {ACCENT_BLUE};
}}
QComboBox::drop-down {{
    border: none;
    width: 24px;
}}
QComboBox::down-arrow {{
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid {TEXT_SECONDARY};
    margin-right: 6px;
}}
QComboBox QAbstractItemView {{
    background: {BG_CARD};
    border: 1px solid {BORDER_COLOR};
    selection-background-color: {ACCENT_BLUE};
    selection-color: {TEXT_PRIMARY};
    color: {TEXT_PRIMARY};
    border-radius: 5px;
    outline: 0px;
    padding: 4px;
}}
QComboBox QAbstractItemView::item {{
    background: {BG_CARD};
    color: {TEXT_PRIMARY};
    padding: 6px 10px;
    min-height: 24px;
}}
QComboBox QAbstractItemView::item:hover {{
    background: {BG_INPUT};
    color: {TEXT_PRIMARY};
}}
QComboBox QAbstractItemView::item:selected {{
    background: {ACCENT_BLUE};
    color: {TEXT_PRIMARY};
}}

/* ── Spin boxes ── */
QSpinBox {{
    background: {BG_INPUT};
    border: 1px solid {BORDER_COLOR};
    border-radius: 5px;
    padding: 7px 10px;
    color: {TEXT_PRIMARY};
    font-size: 13px;
}}
QSpinBox:focus {{
    border: 1px solid {ACCENT_BLUE};
}}
QSpinBox::up-button, QSpinBox::down-button {{
    background: {BG_CARD};
    border: none;
    width: 18px;
    border-radius: 3px;
}}

/* ── Text edits (log area) ── */
QTextEdit {{
    background: #0d1117;
    border: 1px solid {BORDER_COLOR};
    border-radius: 6px;
    color: #58a6ff;
    font-family: 'Consolas', 'Courier New', monospace;
    font-size: 12px;
    padding: 8px;
    selection-background-color: {ACCENT_BLUE};
}}

/* ── Table widget ── */
QTableWidget {{
    background: {BG_SECONDARY};
    border: 1px solid {BORDER_COLOR};
    border-radius: 6px;
    gridline-color: {BORDER_COLOR};
    color: {TEXT_PRIMARY};
    font-size: 12px;
    selection-background-color: {BG_CARD};
}}
QTableWidget::item {{
    padding: 6px 10px;
    border-bottom: 1px solid {BORDER_COLOR};
}}
QTableWidget::item:selected {{
    background: {BG_CARD};
    color: {TEXT_PRIMARY};
}}
QHeaderView::section {{
    background: {BG_PRIMARY};
    color: {TEXT_SECONDARY};
    padding: 8px 10px;
    border: none;
    border-bottom: 1px solid {BORDER_COLOR};
    font-size: 11px;
    font-weight: bold;
    text-transform: uppercase;
    letter-spacing: 1px;
}}

/* ── Progress bar ── */
QProgressBar {{
    background: {BG_INPUT};
    border: 1px solid {BORDER_COLOR};
    border-radius: 4px;
    height: 6px;
    text-align: center;
    font-size: 11px;
    color: transparent;
}}
QProgressBar::chunk {{
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 {ACCENT_BLUE}, stop:1 {ACCENT_GREEN});
    border-radius: 4px;
}}

/* ── Splitter ── */
QSplitter::handle {{
    background: {BORDER_COLOR};
    width: 1px;
}}

/* ── Tool tips ── */
QToolTip {{
    background: {BG_CARD};
    color: {TEXT_PRIMARY};
    border: 1px solid {BORDER_COLOR};
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 12px;
}}

/* ── Menu bar (if used) ── */
QMenuBar {{
    background: {BG_PRIMARY};
    color: {TEXT_PRIMARY};
    border-bottom: 1px solid {BORDER_COLOR};
}}
QMenuBar::item:selected {{
    background: {BG_CARD};
}}
QMenu {{
    background: {BG_CARD};
    border: 1px solid {BORDER_COLOR};
    color: {TEXT_PRIMARY};
}}
QMenu::item:selected {{
    background: {ACCENT_BLUE};
}}
"""

# ── Button styles (applied individually) ──────────────────────────────────────
BTN_PRIMARY = f"""
    QPushButton {{
        background: {ACCENT_BLUE};
        color: white;
        border: none;
        border-radius: 6px;
        padding: 9px 20px;
        font-size: 13px;
        font-weight: bold;
    }}
    QPushButton:hover {{
        background: #5aaefd;
    }}
    QPushButton:pressed {{
        background: #3a8eee;
    }}
    QPushButton:disabled {{
        background: {BG_CARD};
        color: {TEXT_DISABLED};
    }}
"""

BTN_DANGER = f"""
    QPushButton {{
        background: {ACCENT_RED};
        color: white;
        border: none;
        border-radius: 6px;
        padding: 9px 20px;
        font-size: 13px;
        font-weight: bold;
    }}
    QPushButton:hover {{
        background: #f05a4a;
    }}
    QPushButton:pressed {{
        background: #c0392b;
    }}
    QPushButton:disabled {{
        background: {BG_CARD};
        color: {TEXT_DISABLED};
    }}
"""

BTN_SUCCESS = f"""
    QPushButton {{
        background: {ACCENT_GREEN};
        color: white;
        border: none;
        border-radius: 6px;
        padding: 9px 20px;
        font-size: 13px;
        font-weight: bold;
    }}
    QPushButton:hover {{
        background: #25b887;
    }}
    QPushButton:pressed {{
        background: #178a63;
    }}
    QPushButton:disabled {{
        background: {BG_CARD};
        color: {TEXT_DISABLED};
    }}
"""

BTN_OUTLINE = f"""
    QPushButton {{
        background: transparent;
        color: {TEXT_SECONDARY};
        border: 1px solid {BORDER_COLOR};
        border-radius: 6px;
        padding: 7px 16px;
        font-size: 12px;
    }}
    QPushButton:hover {{
        background: {BG_CARD};
        color: {TEXT_PRIMARY};
        border-color: {TEXT_SECONDARY};
    }}
    QPushButton:pressed {{
        background: {BG_INPUT};
    }}
"""

BTN_GHOST = f"""
    QPushButton {{
        background: transparent;
        color: {TEXT_SECONDARY};
        border: none;
        border-radius: 6px;
        padding: 6px 12px;
        font-size: 12px;
    }}
    QPushButton:hover {{
        background: {BG_CARD};
        color: {TEXT_PRIMARY};
    }}
"""
