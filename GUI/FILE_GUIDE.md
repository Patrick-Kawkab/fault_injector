# File guide — what each file does

## App / GUI
- **main.py** — entry point; starts the Qt application and opens the main window.
- **main_window.py** — the main window; wires the config panel to the tabs and starts the campaign worker when you press Run (reads `FI_INJECTOR` to use the real injector, otherwise the mock).
- **config_panel.py** — the left configuration form: sensor, fault type and its per‑type fields (variable / address / value / system state / bit), the Target‑debug dropdowns (machine, CPU, GDB port), number of faults, and the "Vary faults" list builder. Produces a `FaultConfig`.
- **config.py** — the `FaultConfig` dataclass (the contract between GUI and engine), the sensor / safe‑state / ASIL tables, and `to_injector_input()` which emits the `{meta, faults[]}` JSON (including `delay_ms` = the ASIL deadline).
- **styles.py** — colours, fonts and shared style constants.
- **widgets.py** — small reusable UI pieces (section/field labels, dividers, empty‑state, metric cards, AI tag).

## Tabs
- **tab_monitor.py** — live monitor during a run: streaming log, progress bar, and live metric cards (incl. the ASIL deadline card).
- **tab_results.py** — results view: metric cards, fault‑handling‑by‑type chart, and the per‑injection table (no reaction column).
- **tab_report.py** — the ISO 26262 report: summary, compliance table, findings, recommendations, and PDF export.
- **tab_assistant.py** — the AI assistant chat tab (UI).
- **chat_assistant.py** — the assistant's backend: the knowledge base about this framework plus the Gemini API call.

## Orchestrator (the engine)
- **orchestrator/__init__.py** — package entry; exposes `OrchestratorWorker`.
- **orchestrator/worker.py** — the background thread that runs a campaign: brings up the target, runs the injector, streams log/progress lines, and feeds the results manager.
- **orchestrator/injector_interface.py** — builds the injector command and the `{meta, faults[]}` input file, runs the injector (mock or real), reads its result file, and yields result messages. Applies the user‑selected GDB port and fault count.
- **orchestrator/mock_injector.py** — the stand‑in for the C++ injector: reads the input JSON, simulates each fault as PASS/FAIL, writes the result JSON. The default when no real binary is configured.
- **orchestrator/targets.py** — brings up what *we* open per backend: for **Tiva** it launches **OpenOCD** (the debugger) and gates on its port; for **QEMU** it launches **nothing** — the injector opens QEMU itself. No GDB process. Teardown stops whatever was started.
- **orchestrator/proc.py** — process supervision helpers: `wait_for_port` and `SupervisedProcess` (headless launch, output pumped to the log, process‑group teardown).
- **orchestrator/results_manager.py** — turns the injector's PASS/FAIL/ERROR results into the metrics + verdict payload the tabs render (handling rate, ASIL pass/fail, overhead). The injector owns lateness, so there is no timing re‑gate.

## Docs
- **INJECTOR_INTERFACE_CONTRACT.md** — the JSON contract with the C++ team (invocation, `{meta, faults[]}` input/output, per‑type field use).
- **README.md / INSTALLATION GUIDE.md** — existing project docs.
