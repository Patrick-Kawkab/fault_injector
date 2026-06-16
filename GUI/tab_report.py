"""
tab_report.py
-------------
AI Report tab — shows the auto-generated ISO 26262 compliance report.

Changes:
  - Export PDF button saves a real PDF file using QPrinter
  - Copy button copies the full report as plain text to the clipboard
  - Metric cards are taller
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel,
    QScrollArea, QFrame, QPushButton, QSizePolicy,
    QFileDialog, QMessageBox, QApplication
)
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QFont
from PyQt5.QtPrintSupport import QPrinter, QPrintDialog
from datetime import datetime

import styles
from widgets import EmptyState, CardFrame, CardTitle, StatusBadge
from config import FaultConfig, FAULT_TYPES, HARDWARE_MODES


def _generate_report_text(data: dict) -> dict:
    cfg       = data["config"]
    total     = data["total"]
    handled   = data["handled"]
    coverage  = data["handling_pct"]
    avg_lat   = data["avg_reaction_ms"]
    max_lat   = data["max_reaction_ms"]
    ftti      = data.get("ftti_ms", 10)
    overhead  = data["overhead_pct"]
    passed    = data["passed_asil"]
    cases     = data["test_cases"]
    fault_lbl = FAULT_TYPES.get(cfg.fault_type, cfg.fault_type)
    hw_lbl    = HARDWARE_MODES.get(cfg.hardware, cfg.hardware)
    missed    = total - handled

    summary = (
        f"This report presents the results of a fault injection campaign targeting the "
        f"<b>{cfg.sensor}</b> sensor on <b>{hw_lbl}</b> hardware. "
        f"The campaign was conducted in accordance with <b>ISO 26262 Part 6</b> software "
        f"safety requirements for <b>{cfg.asil_level}</b> systems. "
        f"A total of <b>{total} fault injection test cases</b> were executed using the "
        f"<b>{fault_lbl}</b> fault model over a {cfg.duration_s}-second injection window per test."
    )

    criteria = data.get("criteria")
    if criteria:
        compliance = [
            (f"{name} ({target})", value,
             "ok" if ok else "fail", "Met ✓" if ok else "Not met ✗")
            for (name, value, target, ok) in criteria
        ]
    else:
        compliance = [
            ("Fault-handling rate", f"{coverage}%",
             "ok" if coverage >= 90 else "fail", "Met ✓" if coverage >= 90 else "Not met ✗"),
            ("Runtime overhead (<5%)", f"{overhead}%",
             "ok" if overhead < 5 else "fail", "Met ✓" if overhead < 5 else "Not met ✗"),
        ]

    if passed:
        findings = [("ok",
            f"The {cfg.sensor} safety mechanism met all ISO 26262 {cfg.asil_level} acceptance "
            f"criteria: the system reached its safe state for {handled}/{total} injected faults "
            f"({coverage}%), with a worst-case reaction time of {max_lat}ms (within the {ftti}ms FTTI) "
            f"and {overhead}% runtime overhead — each within its target."
        )]
    else:
        failed = [name for (name, value, target, ok) in (criteria or []) if not ok]
        failed_txt = ", ".join(failed) if failed else "one or more acceptance criteria"
        findings = [("warn",
            f"The campaign did not meet all ISO 26262 {cfg.asil_level} acceptance criteria "
            f"(failing: {failed_txt}). The system mitigated {handled}/{total} injected faults "
            f"({coverage}%), worst-case reaction {max_lat}ms (FTTI {ftti}ms), overhead {overhead}%. "
            f"The gap(s) must be addressed "
            f"before certification can proceed."
        )]

    if missed > 0:
        findings.append(("warn",
            f"{missed} injected fault(s) were not mitigated — the system did not reach the safe "
            f"state within the {ftti}ms FTTI (faulty value propagated to the controller). "
            "Review the safety mechanism implementation for the affected fault scenarios."
        ))

    recommendations = [
        f"{'Re-validate' if passed else 'Tighten'} the {cfg.sensor} plausibility check "
        f"thresholds to {'confirm' if passed else 'achieve'} a ≥90% fault-handling rate across all fault types.",
        f"Expand the test campaign to include multi-fault scenarios — e.g. simultaneous "
        f"{fault_lbl.lower()} and timing violation — to validate compound failure behavior "
        f"as required by {cfg.asil_level} latent fault analysis.",
        f"{'Generate and submit' if passed else 'Re-run the campaign after fixes and generate'} "
        f"an updated compliance evidence package for certification submission, including "
        f"traceability matrices linking these results to safety requirements.",
    ]

    return {
        "summary": summary,
        "compliance": compliance,
        "findings": findings,
        "recommendations": recommendations,
        "inline_metrics": [
            (f"{coverage}%", "Handling rate",
             styles.ACCENT_GREEN if coverage >= 90 else styles.ACCENT_RED),
            (str(total),     "Tests run",      styles.ACCENT_BLUE),
            (f"{avg_lat}ms", "Avg reaction",   styles.ACCENT_AMBER),
            (f"{overhead}%", "Overhead",       styles.ACCENT_GREEN),
        ]
    }


def _build_plain_text(data: dict, report: dict, now: str) -> str:
    """Build a plain-text version of the report for clipboard copy."""
    cfg      = data["config"]
    hw_lbl   = HARDWARE_MODES.get(cfg.hardware, cfg.hardware)
    lines = [
        "=" * 70,
        f"FAULT INJECTION TEST REPORT — {cfg.sensor} SENSOR CAMPAIGN",
        f"Generated: {now}",
        f"Hardware: {hw_lbl}  |  Standard: ISO 26262  |  {cfg.asil_level}",
        f"Campaign: {cfg.sensor}_{cfg.fault_type}",
        "=" * 70,
        "",
        "EXECUTIVE SUMMARY",
        "-" * 40,
    ]
    # Strip HTML tags from summary
    import re
    plain_summary = re.sub(r'<[^>]+>', '', report["summary"])
    lines.append(plain_summary)
    lines.append("")

    lines += ["KEY METRICS", "-" * 40]
    for val, label, _ in report["inline_metrics"]:
        lines.append(f"  {label}: {val}")
    lines.append("")

    lines += ["ISO 26262 COMPLIANCE", "-" * 40]
    for req, val, status, note in report["compliance"]:
        lines.append(f"  [{note}]  {req}: {val}")
    lines.append("")

    lines += ["KEY FINDINGS", "-" * 40]
    for status, text in report["findings"]:
        icon = "✓" if status == "ok" else "⚠"
        lines.append(f"  {icon} {text}")
    lines.append("")

    lines += ["RECOMMENDATIONS", "-" * 40]
    for i, rec in enumerate(report["recommendations"], 1):
        lines.append(f"  {i}. {rec}")
    lines.append("")
    lines.append("=" * 70)
    return "\n".join(lines)


def _build_html_report(data: dict, report: dict, now: str) -> str:
    """Build a full HTML document for PDF export."""
    cfg    = data["config"]
    hw_lbl = HARDWARE_MODES.get(cfg.hardware, cfg.hardware)
    import re

    rows = ""
    for req, val, status, note in report["compliance"]:
        color = "#1d9e75" if status == "ok" else ("#f0a500" if status == "warn" else "#e74c3c")
        rows += (f"<tr><td>{req}</td><td>{val}</td>"
                 f"<td style='color:{color};font-weight:bold'>{note}</td></tr>")

    findings_html = ""
    for status, text in report["findings"]:
        bg    = "#0d3028" if status == "ok" else "#2a1f00"
        color = "#1d9e75" if status == "ok" else "#f0a500"
        icon  = "✓" if status == "ok" else "⚠"
        findings_html += f'<div style="background:{bg};border:1px solid {color};border-radius:6px;padding:12px;margin-bottom:8px;color:{color}">{icon} {text}</div>'

    recs_html = "".join(
        f'<p style="color:#9e9e9e"><b style="color:#4a9eff">{i}.</b> {rec}</p>'
        for i, rec in enumerate(report["recommendations"], 1)
    )

    plain_summary = re.sub(r'<[^>]+>', '', report["summary"])

    return f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
  body {{ font-family: Arial, sans-serif; background: #1a1a2e; color: #e8eaf6; margin: 40px; }}
  h1 {{ font-size: 20px; color: #e8eaf6; }}
  h2 {{ font-size: 15px; color: #9e9e9e; border-bottom: 1px solid #2a2a4a; padding-bottom: 6px; margin-top: 28px; }}
  .meta {{ color: #9e9e9e; font-size: 12px; margin-bottom: 20px; }}
  table {{ font-size: 11pt; }}
  th {{ font-size: 9pt; color: #9e9e9e; background: #16213e; padding: 8px 12px; text-align: left; }}
  td {{ font-size: 11pt; padding: 7px 12px; }}
  .mval {{ font-size: 19pt; font-weight: bold; }}
  .mlbl {{ font-size: 9pt; color: #9e9e9e; }}
</style>
</head>
<body>
<h1>Fault Injection Test Report — {cfg.sensor} Sensor Campaign</h1>
<div class="meta">Generated: {now} &nbsp;|&nbsp; Hardware: {hw_lbl} &nbsp;|&nbsp; ISO 26262 &nbsp;|&nbsp; {cfg.asil_level}</div>

<h2>Executive Summary</h2>
<p style="color:#9e9e9e;line-height:1.7">{plain_summary}</p>

<h2>Key Metrics</h2>
<table width="100%" cellspacing="10"><tr>
{''.join(f'<td width="25%" bgcolor="#0f3460" align="center"><span class="mval" style="color:{c}">{v}</span><br><span class="mlbl">{l}</span></td>' for v, l, c in report["inline_metrics"])}
</tr></table>

<h2>ISO 26262 Compliance</h2>
<table width="100%"><tr><th>Requirement</th><th>Value</th><th>Status</th></tr>{rows}</table>

<h2>Key Findings</h2>
{findings_html}

<h2>Recommendations</h2>
{recs_html}
</body>
</html>"""


class ReportTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._root = QVBoxLayout(self)
        self._root.setContentsMargins(14, 14, 14, 14)
        self._root.setSpacing(12)
        self._data   = None
        self._report = None
        self._now    = None
        self._show_empty()

    def _show_empty(self):
        self._clear()
        empty = EmptyState(
            "📄",
            "No report yet",
            "The AI will generate a full ISO 26262-compliant report\n"
            "automatically once your test campaign is complete."
        )
        self._root.addWidget(empty)

    def show_report(self, data: dict):
        self._clear()
        self._data   = data
        self._report = _generate_report_text(data)
        self._now    = datetime.now().strftime("%d %B %Y, %H:%M")

        report = self._report
        cfg    = data["config"]
        hw_lbl = HARDWARE_MODES.get(cfg.hardware, cfg.hardware)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setStyleSheet("background: transparent;")

        container = QWidget()
        container.setStyleSheet("background: transparent;")
        vl = QVBoxLayout(container)
        vl.setContentsMargins(0, 0, 0, 0)
        vl.setSpacing(14)

        report_card = QFrame()
        report_card.setStyleSheet(f"""
            QFrame {{
                background: {styles.BG_SECONDARY};
                border: 1px solid {styles.BORDER_COLOR};
                border-radius: 10px;
            }}
        """)
        rl = QVBoxLayout(report_card)
        rl.setContentsMargins(20, 18, 20, 20)
        rl.setSpacing(14)

        # ── Header ──
        header_row = QHBoxLayout()
        title_col  = QVBoxLayout()

        title_lbl = QLabel(f"Fault Injection Test Report — {cfg.sensor} Sensor Campaign")
        title_lbl.setStyleSheet(f"font-size: 15px; font-weight: bold; color: {styles.TEXT_PRIMARY}; background: transparent;")
        title_lbl.setWordWrap(True)

        meta1_row = QHBoxLayout()
        meta1 = QLabel(f"Generated: {self._now}")
        meta1.setStyleSheet(f"color: {styles.TEXT_SECONDARY}; font-size: 11px; background: transparent;")
        ai_badge = QLabel("✦ AI generated")
        ai_badge.setStyleSheet(f"""
            color: {styles.ACCENT_BLUE}; background: #1a2a40;
            border: 1px solid {styles.ACCENT_BLUE}; border-radius: 10px;
            padding: 2px 10px; font-size: 10px; font-weight: bold;
        """)
        meta1_row.addWidget(meta1)
        meta1_row.addWidget(ai_badge)
        meta1_row.addStretch()

        meta2 = QLabel(f"Hardware: {hw_lbl}  ·  ISO 26262  ·  {cfg.asil_level}  ·  Campaign: {cfg.sensor}_{cfg.fault_type}")
        meta2.setStyleSheet(f"color: {styles.TEXT_DISABLED}; font-size: 11px; background: transparent;")

        title_col.addWidget(title_lbl)
        title_col.addLayout(meta1_row)
        title_col.addWidget(meta2)

        # ── Action buttons ──
        export_col = QVBoxLayout()
        export_col.setAlignment(Qt.AlignTop)
        export_col.setSpacing(6)

        pdf_btn  = QPushButton("⬇  Export PDF")
        copy_btn = QPushButton("⎘  Copy text")
        for btn in [pdf_btn, copy_btn]:
            btn.setStyleSheet(styles.BTN_OUTLINE)
            btn.setFixedHeight(30)
            btn.setFixedWidth(120)

        pdf_btn.clicked.connect(self._export_pdf)
        copy_btn.clicked.connect(self._copy_text)

        export_col.addWidget(pdf_btn)
        export_col.addWidget(copy_btn)

        header_row.addLayout(title_col, stretch=1)
        header_row.addLayout(export_col)
        rl.addLayout(header_row)
        rl.addWidget(self._divider())

        # ── Executive summary ──
        rl.addWidget(self._section_title("📋  Executive Summary"))
        summary_lbl = QLabel(report["summary"])
        summary_lbl.setWordWrap(True)
        summary_lbl.setStyleSheet(f"color: {styles.TEXT_SECONDARY}; font-size: 12px; line-height: 1.7; background: transparent;")
        summary_lbl.setTextFormat(Qt.RichText)
        rl.addWidget(summary_lbl)

        # Inline metrics (taller)
        metrics_row = QHBoxLayout()
        metrics_row.setSpacing(16)
        for val, label, color in report["inline_metrics"]:
            col = QVBoxLayout()
            v = QLabel(val)
            v.setStyleSheet(f"color: {color}; font-size: 22px; font-weight: bold; background: transparent;")
            l = QLabel(label)
            l.setStyleSheet(f"color: {styles.TEXT_DISABLED}; font-size: 10px; background: transparent;")
            col.addWidget(v)
            col.addWidget(l)
            metrics_row.addLayout(col)
        metrics_row.addStretch()
        rl.addLayout(metrics_row)
        rl.addWidget(self._divider())

        # ── Compliance ──
        rl.addWidget(self._section_title("🛡  ISO 26262 Compliance Assessment"))
        for req, value, status, note in report["compliance"]:
            crow = QHBoxLayout()
            req_lbl = QLabel(req)
            req_lbl.setStyleSheet(f"color: {styles.TEXT_SECONDARY}; font-size: 12px; background: transparent;")
            req_lbl.setWordWrap(True)
            badge = StatusBadge(f"{value} — {note}", status)
            badge.setFixedHeight(22)
            crow.addWidget(req_lbl, stretch=1)
            crow.addWidget(badge)
            rl.addLayout(crow)
            rl.addWidget(self._divider())

        # ── Findings ──
        rl.addWidget(self._section_title("💡  Key Findings"))
        for status, text in report["findings"]:
            bg    = "#0d3028" if status == "ok" else "#2a1f00"
            color = styles.ACCENT_GREEN if status == "ok" else styles.ACCENT_AMBER
            icon  = "✓" if status == "ok" else "⚠"
            finding = QFrame()
            finding.setStyleSheet(f"QFrame {{ background: {bg}; border: 1px solid {color}; border-radius: 6px; }}")
            fl = QHBoxLayout(finding)
            fl.setContentsMargins(12, 10, 12, 10)
            icon_lbl = QLabel(icon)
            icon_lbl.setStyleSheet(f"color: {color}; font-size: 16px; background: transparent;")
            icon_lbl.setFixedWidth(20)
            icon_lbl.setAlignment(Qt.AlignTop | Qt.AlignHCenter)
            text_lbl = QLabel(text)
            text_lbl.setWordWrap(True)
            text_lbl.setStyleSheet(f"color: {color}; font-size: 12px; background: transparent;")
            fl.addWidget(icon_lbl)
            fl.addWidget(text_lbl, stretch=1)
            rl.addWidget(finding)

        rl.addWidget(self._divider())

        # ── Recommendations ──
        rl.addWidget(self._section_title("✅  Recommendations"))
        for i, rec in enumerate(report["recommendations"], 1):
            rrow = QHBoxLayout()
            num_lbl = QLabel(str(i))
            num_lbl.setFixedSize(22, 22)
            num_lbl.setAlignment(Qt.AlignCenter)
            num_lbl.setStyleSheet(f"""
                color: {styles.ACCENT_BLUE}; background: #1a2a40;
                border: 1px solid {styles.ACCENT_BLUE}; border-radius: 11px;
                font-size: 11px; font-weight: bold;
            """)
            rec_lbl = QLabel(rec)
            rec_lbl.setWordWrap(True)
            rec_lbl.setStyleSheet(f"color: {styles.TEXT_SECONDARY}; font-size: 12px; background: transparent;")
            rrow.addWidget(num_lbl)
            rrow.addWidget(rec_lbl, stretch=1)
            rl.addLayout(rrow)

        vl.addWidget(report_card)
        scroll.setWidget(container)
        self._root.addWidget(scroll)

    # ── Export PDF ────────────────────────────────────────────────────────────
    def _export_pdf(self):
        if not self._data:
            return
        cfg = self._data["config"]
        default_name = f"FI_Report_{cfg.sensor}_{cfg.fault_type}_{datetime.now().strftime('%Y%m%d_%H%M')}.pdf"
        path, _ = QFileDialog.getSaveFileName(
            self, "Export PDF Report", default_name, "PDF Files (*.pdf)"
        )
        if not path:
            return
        try:
            printer = QPrinter(QPrinter.HighResolution)
            printer.setOutputFormat(QPrinter.PdfFormat)
            printer.setOutputFileName(path)
            printer.setPageSize(QPrinter.A4)

            from PyQt5.QtWidgets import QTextEdit
            doc = QTextEdit()
            html = _build_html_report(self._data, self._report, self._now)
            doc.setHtml(html)
            doc.print_(printer)

            QMessageBox.information(self, "PDF exported", f"Report saved to:\n{path}")
        except Exception as e:
            QMessageBox.critical(self, "Export failed", str(e))

    # ── Copy to clipboard ─────────────────────────────────────────────────────
    def _copy_text(self):
        if not self._data:
            return
        plain = _build_plain_text(self._data, self._report, self._now)
        QApplication.clipboard().setText(plain)
        QMessageBox.information(
            self, "Copied",
            "Report copied to clipboard as plain text.\nYou can paste it into any document or email."
        )

    # ── Helpers ───────────────────────────────────────────────────────────────
    def _section_title(self, text: str) -> QLabel:
        lbl = QLabel(text)
        lbl.setStyleSheet(f"""
            color: {styles.TEXT_PRIMARY}; font-size: 13px; font-weight: bold;
            background: transparent; padding-bottom: 4px;
            border-bottom: 1px solid {styles.BORDER_COLOR};
        """)
        return lbl

    def _divider(self) -> QFrame:
        d = QFrame()
        d.setFrameShape(QFrame.HLine)
        d.setFixedHeight(1)
        d.setStyleSheet(f"background: {styles.BORDER_COLOR}; border: none;")
        return d

    def _clear(self):
        while self._root.count():
            item = self._root.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
