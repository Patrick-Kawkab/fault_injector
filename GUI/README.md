# Fault Injection Framework — GUI

A PyQt5 desktop GUI for the Fault Injection Framework.
Built for BNU Mechatronics & Automation — Senior Project.

---

## Project structure

```
fault_injection_gui/
├── main.py             ← Entry point, run this
├── main_window.py      ← Main window, wires everything together
├── config_panel.py     ← Left panel: manual fault configuration form
├── tab_monitor.py      ← Live monitor tab (log, charts, progress)
├── tab_results.py      ← Results tab (metrics, table, charts)
├── tab_report.py       ← AI report tab (ISO 26262 compliance report)
├── mock_worker.py      ← Mock QThread simulating the orchestrator
├── config.py           ← FaultConfig dataclass + sensor knowledge base
├── widgets.py          ← Reusable custom widgets
├── styles.py           ← All colors, fonts, and stylesheets
└── README.md
```

---

## Setup

```bash
pip install PyQt5
python main.py
```

---

## How to use

1. **Fill in the form** on the left panel — select sensor, fault type, address, timing
2. **Press Run injection** — the Live monitor tab shows a confirmation banner
3. **Confirm** to start the injection — the log fills in real time
4. **Switch to Results** tab to see the full breakdown and charts
5. **Switch to AI report** tab for the ISO 26262 compliance report

---

## How to connect the real orchestrator

The GUI currently uses `mock_worker.py` to simulate injection.
To connect the real C++ injector:

1. Open `mock_worker.py`
2. Replace the body of `MockInjectionWorker.run()` with:
   - Write `self.config` to `config.json` using `self.config.to_file("config.json")`
   - Launch your C++ injector as a subprocess
   - Stream stdout line by line and emit each line via `self.log_line.emit(line)`
   - When done, parse `results.json` and emit via `self.finished.emit(results_dict)`

The signal names and the results dict structure must match what `main_window.py` expects:
```python
worker.log_line.connect(...)    # str
worker.progress.connect(...)    # int 0–100
worker.finished.connect(...)    # dict (see mock_worker.py for structure)
worker.error.connect(...)       # str
```

---

## How to add the AI agent layer

The AI input box is not included in this version (manual form only).
To add it later:

1. Add a `QTextEdit` AI input box at the top of `config_panel.py`
2. On submit, call the LLM API with the user's text + SYSTEM_PROMPT
3. Parse the returned JSON into a `FaultConfig`
4. Call `self._populate_from_config(cfg)` to fill all form fields
5. Show the confirmation banner so the user can verify before running

---

## Signals contract (GUI ↔ Orchestrator)

| Signal | Type | Description |
|--------|------|-------------|
| `log_line` | `str` | One line of log output |
| `progress` | `int` | 0–100 percent complete |
| `finished` | `dict` | Full results when done |
| `error` | `str` | Error message |

---

## FaultConfig fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `hardware` | str | "tivac" | "tivac" or "qemu" |
| `sensor` | str | "" | e.g. "WSS", "TPS" |
| `fault_type` | str | "" | "stuck_zero", "stuck_max", "out_of_range", "noisy", "bit_flip" |
| `address` | str | "" | e.g. "0x0000011F" |
| `duration_s` | int | 60 | seconds |
| `delay_ms` | int | 0 | ms before injection |
| `min_value` | int | 0 | sensor min |
| `max_value` | int | 255 | sensor max |
| `fault_value` | int | 0 | value to inject |
| `bit_position` | int | 0 | bit to flip |
| `asil_level` | str | "ASIL-D" | ASIL-A/B/C/D |
| `expected_behavior` | str | "" | human description |
