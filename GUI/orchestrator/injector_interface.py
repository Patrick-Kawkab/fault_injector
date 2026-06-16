"""
injector_interface.py
=====================
The bridge between the Python orchestrator and the C++ fault injector.

It writes the GUI's ``FaultConfig`` to the input JSON the injector reads, spawns
the injector as a subprocess, and collects the results. See
INJECTOR_INTERFACE_CONTRACT.md for the full schema.

Two transports are supported, transparently:
  - **file** (the real injector): writes ``campaign_result.json`` (``--out``)
    and exits. The interface reads that file and replays each fault as a
    ``result`` message. Optional ``log`` / ``progress`` lines on stdout drive
    the live monitor while it runs.
  - **stream** (legacy): emits ``result`` / ``done`` lines on stdout directly.

Either way the worker consumes the same message types: ``log``, ``progress``,
``result``, ``done``, ``error``.

Invocation:
    <injector> --config <in.json> --out <result.json> --backend <tiva|qemu> --gdb <host:port>
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import threading
from datetime import datetime
from pathlib import Path
from typing import Iterator, List, Optional

from config import CAMPAIGN_SIZE

MOCK_INJECTOR = str(Path(__file__).with_name("mock_injector.py"))


class InjectorError(RuntimeError):
    pass


class InjectorInterface:
    def __init__(
        self,
        config,
        target,
        injector_binary: Optional[str] = None,
        use_mock: bool = False,
        workdir: Optional[str] = None,
    ):
        self.config = config
        self.target = target

        if workdir:
            self.workdir = workdir
        else:
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            self.workdir = os.path.join("runs", f"run_{ts}")
        os.makedirs(self.workdir, exist_ok=True)

        self.config_path = os.path.join(self.workdir, "fault_config.json")
        self.result_path = os.path.join(self.workdir, "campaign_result.json")

        self._proc: Optional[subprocess.Popen] = None
        self._lock = threading.Lock()
        self._aborted = False

        # The endpoint the injector dials — user-selected port (config.gdb_port),
        # overriding the target's default so the UI dropdown is authoritative.
        args = target.injector_args()
        self._gdb = f"localhost:{config.gdb_port}"
        if "--gdb" in args:
            args[args.index("--gdb") + 1] = self._gdb

        # Resolve the command once, up front, so the worker can ask whether
        # we are running the mock before deciding to bring up real hardware.
        if use_mock or not injector_binary or not os.path.exists(injector_binary):
            base = [sys.executable, MOCK_INJECTOR]
            self.using_mock = True
        else:
            base = [injector_binary]
            self.using_mock = False
        self._cmd: List[str] = base + [
            "--config", self.config_path,
            "--out", self.result_path,
        ] + args

    def _write_config(self) -> None:
        payload = self.config.to_injector_input()
        with open(self.config_path, "w") as fh:
            json.dump(payload, fh, indent=2)

    def stream(self) -> Iterator[dict]:
        """Spawn the injector and yield parsed protocol messages.

        The generator owns the subprocess and always terminates it in
        ``finally`` — on normal completion, on exception, or if the caller
        breaks out early. ``terminate()`` may be called from another thread.
        """
        self._write_config()
        with self._lock:
            self._proc = subprocess.Popen(
                self._cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,  # surface injector diagnostics as logs
                text=True,
                bufsize=1,
            )
        proc = self._proc
        streamed_results = False
        try:
            assert proc.stdout is not None
            for raw in proc.stdout:
                line = raw.strip()
                if not line:
                    continue
                try:
                    msg = json.loads(line)
                    if not isinstance(msg, dict) or "type" not in msg:
                        msg = {"type": "log", "message": line}
                except json.JSONDecodeError:
                    msg = {"type": "log", "message": line}
                if msg.get("type") in ("result", "done"):
                    streamed_results = True
                yield msg

            ret = proc.wait()
            if ret not in (0, None) and not self._aborted:
                yield {"type": "error", "message": f"injector exited with code {ret}"}
                return

            # File transport: results live in the --out file, not on stdout.
            if not streamed_results and not self._aborted:
                yield from self._read_results()
        finally:
            self.terminate()

    def _read_results(self) -> Iterator[dict]:
        """Read the campaign result file and replay it as result messages."""
        if not os.path.exists(self.result_path):
            yield {"type": "error",
                   "message": f"injector produced no result file at {self.result_path}"}
            return
        try:
            with open(self.result_path) as fh:
                data = json.load(fh)
        except (OSError, json.JSONDecodeError) as exc:
            yield {"type": "error", "message": f"could not read result file: {exc}"}
            return

        faults = data.get("faults", [])
        yield {"type": "log",
               "message": f"[OUT]  Read {len(faults)} results from {os.path.basename(self.result_path)}"}
        for f in faults:
            if isinstance(f, dict):
                msg = dict(f)
                msg["type"] = "result"
                yield msg
        meta = data.get("meta", {}) if isinstance(data, dict) else {}
        yield {"type": "done", "overhead_pct": meta.get("overhead_pct")}

    def terminate(self) -> None:
        """Kill the injector if running. Thread-safe; used by the GUI to abort.
        Killing the process closes its stdout, unblocking the read loop above."""
        with self._lock:
            proc, self._proc = self._proc, None
        if proc and proc.poll() is None:
            self._aborted = True
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
