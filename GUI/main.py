"""
main.py
-------
Entry point for the Fault Injection Framework GUI.

Usage:
    python main.py

Requirements:
    pip install PyQt5
"""

import sys
import os
from PyQt5.QtWidgets import QApplication
from PyQt5.QtGui import QFont
from PyQt5.QtCore import Qt

import styles
from main_window import MainWindow


def main():
    # Fix "QSocketNotifier: Can only be used with threads started with QThread"
    # and "Wayland does not support QWindow::requestActivate()" warnings.
    # Force the xcb (X11) backend when running on Wayland, unless the user
    # has already set QT_QPA_PLATFORM themselves.
    if "WAYLAND_DISPLAY" in os.environ and "QT_QPA_PLATFORM" not in os.environ:
        os.environ["QT_QPA_PLATFORM"] = "xcb"

    # High DPI support — must be set BEFORE QApplication is constructed
    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling, True)
    QApplication.setAttribute(Qt.AA_UseHighDpiPixmaps, True)
    QApplication.setAttribute(Qt.AA_ShareOpenGLContexts, True)

    # Use Qt's built-in Fusion style instead of the system GTK bridge.
    # This is the most reliable way to prevent Linux-specific rendering issues
    # where popup/dropdown widgets ignore the app stylesheet (white background).
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    app.setApplicationName("Fault Injection Framework")
    app.setOrganizationName("BNU Mechatronics")

    # Apply global stylesheet BEFORE creating any widgets so the dark theme
    # propagates to every child widget (combos, inputs, cards, etc.).
    # Without this ordering some widgets render with the system default (white).
    app.setStyleSheet(styles.APP_STYLE)

    # Default font
    font = QFont("Segoe UI", 10)
    app.setFont(font)

    window = MainWindow()
    window.show()

    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
