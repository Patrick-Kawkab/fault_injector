"""
worker.py
=========
``OrchestratorWorker`` — a drop-in replacement for ``MockInjectionWorker``.

It exposes the exact same signals (``log_line``, ``progress``, ``finished``,
``error``), takes the GUI's ``config.FaultConfig``, and has a ``stop()`` method,
so swapping it into the GUI is a one-line import change. The difference is what
happens inside ``run()``: instead of fabricating results, it launches the real
C++ injector through the orchestrator (target -> injector -> results) and
streams the outcome back. Until a C++ binary is configured, it automatically
falls back to the bundled mock injector, so the GUI runs end-to-end today.

Usage in the GUI (in MainWindow._on_confirmed), unchanged except the class:
    self._worker = OrchestratorWorker(cfg)                 # mock fallback
    # or, once the binary exists:
    self._worker = OrchestratorWorker(cfg, injector_binary="/path/to/injector")
    self._worker.log_line.connect(self._right_panel.monitor_tab.append_log)
    self._worker.progress.connect(self._right_panel.monitor_tab.set_progress)
    self._worker.finished.connect(self._on_finished)
    self._worker.error.connect(self._on_error)
    self._worker.start()
"""

from __future__ import annotations

from typing import Optional

from PyQt5.QtCore import QThread, pyqtSignal

from config import FaultConfig, FAULT_TYPES, HARDWARE_MODES, expected_safe_state

from .injector_interface import InjectorInterface
from .results_manager import ResultsManager
from .targets import BaseTarget, TargetError, TargetFactory


class OrchestratorWorker(QThread):
    # identical signal contract to MockInjectionWorker
    log_line = pyqtSignal(str)
    progress = pyqtSignal(int)      # 0-100 percent
    finished = pyqtSignal(dict)
    error = pyqtSignal(str)

    def __init__(
        self,
        config: FaultConfig,
        parent=None,
        *,
        injector_binary: Optional[str] = None,
        use_mock: bool = False,
    ):
        super().__init__(parent)
        self.config = config
        self._injector_binary = injector_binary
        self._use_mock = use_mock
        self._injector: Optional[InjectorInterface] = None
        self._running = True

    # -- abort, callable from the GUI thread (matches MockInjectionWorker) ---
    def stop(self) -> None:
        self._running = False
        injector = self._injector
        if injector is not None:
            injector.terminate()  # unblocks the read loop in run()

    # -- QThread body -------------------------------------------------------
    def run(self) -> None:
        cfg = self.config
        target: Optional[BaseTarget] = None
        fault_label = FAULT_TYPES.get(cfg.fault_type, cfg.fault_type)
        hw_label = HARDWARE_MODES.get(cfg.hardware, cfg.hardware)
        try:
            self.log_line.emit("[INFO]  Starting fault injection campaign")
            self.log_line.emit(f"[INFO]  Hardware:   {hw_label}")
            self.log_line.emit(f"[INFO]  Sensor:     {cfg.sensor}")
            self.log_line.emit(f"[INFO]  Fault type: {fault_label}")
            self.log_line.emit(f"[INFO]  Target:     {cfg.variable or cfg.address}")
            self.log_line.emit(f"[INFO]  Duration:   {cfg.duration_s}s")
            self.log_line.emit(f"[INFO]  Inject:     hold {cfg.duration_ms}ms, re-inject every {cfg.interval_ms}ms")
            expected = cfg.expected_behavior.strip() or expected_safe_state(cfg.sensor, cfg.fault_type)
            self.log_line.emit(f"[INFO]  Expected safe state: {expected}")

            target = TargetFactory.create(cfg)
            target.set_log_callback(self.log_line.emit)
            self._injector = InjectorInterface(
                cfg, target,
                injector_binary=self._injector_binary,
                use_mock=self._use_mock,
            )
            results = ResultsManager(cfg)

            # Only bring up real QEMU/hardware for a real campaign. In mock mode
            # we just narrate the connection so the demo runs with no backend.
            if self._injector.using_mock:
                self.log_line.emit("[INFO]  No C++ injector configured — using mock injector")
                self.log_line.emit(f"[CONN]  Simulating {target.name} backend")
            else:
                self.log_line.emit(f"[CONN]  Bringing up {target.name}...")
                target.setup()
                self.log_line.emit("[CONN]  Target ready")
            self.progress.emit(10)

            for msg in self._injector.stream():
                if not self._running:
                    break
                kind = msg.get("type")
                if kind == "log":
                    self.log_line.emit(str(msg.get("message", "")))
                elif kind == "progress":
                    total = max(int(msg.get("total", 1)), 1)
                    cur = int(msg.get("current", 0))
                    self.progress.emit(min(10 + int(cur / total * 85), 95))
                elif kind == "result":
                    case = results.add_result(msg)
                    # Log lines the monitor tab parses for its live metrics:
                    # pass -> contains "✓" and "SAFE STATE"
                    # fail -> contains "✗" and "FAILED"
                    if case["outcome"] == "Pass":
                        self.log_line.emit(
                            f"[{case['id']}] ✓ SAFE STATE reached — "
                            f"{case['system_response']}"
                        )
                    elif case["outcome"] == "Error":
                        self.log_line.emit(
                            f"[{case['id']}] ⚠ INJECTION ERROR — {case['fail_reason']}"
                        )
                    else:
                        self.log_line.emit(
                            f"[{case['id']}] ✗ SYSTEM FAILED — {case['fail_reason']}"
                        )
                elif kind == "done":
                    results.set_overhead(msg.get("overhead_pct"))
                elif kind == "error":
                    raise RuntimeError(msg.get("message", "injector reported an error"))

            if not self._running:
                self.log_line.emit("[STOP]  Injection stopped by user")
                return

            payload = results.build_payload()
            self.progress.emit(100)
            self.log_line.emit("[DONE]  Campaign complete")
            self.log_line.emit(
                f"[DONE]  {payload['handled']}/{payload['total']} faults handled "
                f"(safe state reached within FTTI)"
            )
            self.finished.emit(payload)

        except (TargetError, RuntimeError, ValueError) as exc:
            self.error.emit(str(exc))
        except Exception as exc:  # last-resort guard: the thread must not die silently
            self.error.emit(f"Unexpected error: {exc}")
        finally:
            if self._injector is not None:
                self._injector.terminate()
            if target is not None:
                target.teardown()
