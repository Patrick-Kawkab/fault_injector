"""
proc.py
=======
Phase-2 process supervision. Brings background processes (OpenOCD, GDB, QEMU)
up headless, gates on readiness (an open TCP port or a marker line in stdout),
forwards their output to the GUI log, keeps them alive via an open stdin, and
tears them down as a process group.

None of this runs in mock mode — the worker only calls ``target.setup()`` for a
real campaign.
"""

from __future__ import annotations

import os
import signal
import socket
import subprocess
import threading
import time
from typing import Callable, List, Optional


def wait_for_port(host: str, port: int, timeout: float = 20.0, interval: float = 0.15) -> bool:
    """Block until a TCP connection to host:port succeeds, or ``timeout`` elapses."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1.0):
                return True
        except OSError:
            time.sleep(interval)
    return False


class SupervisedProcess:
    """A background process whose stdout is pumped to a callback line by line.

    - ``ready_marker`` (optional): a substring that signals the process is ready
      (used for GDB, which exposes no port). ``wait_ready`` blocks until it
      appears or the process dies.
    - stdin is held open so a resident process (e.g. GDB at its prompt) does not
      hit EOF and exit.
    - ``stop`` kills the whole process group, so children (OpenOCD's gdb server,
      etc.) go down with it.
    """

    def __init__(
        self,
        name: str,
        cmd: List[str],
        on_line: Optional[Callable[[str], None]] = None,
        ready_marker: Optional[str] = None,
        cwd: Optional[str] = None,
    ):
        self.name = name
        self.cmd = cmd
        self.on_line = on_line
        self.ready_marker = ready_marker
        self.cwd = cwd
        self.proc: Optional[subprocess.Popen] = None
        self._ready = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        kwargs = dict(
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            cwd=self.cwd,
        )
        if os.name == "posix":
            kwargs["start_new_session"] = True  # own process group → killpg on stop
        self.proc = subprocess.Popen(self.cmd, **kwargs)
        self._thread = threading.Thread(target=self._pump, daemon=True)
        self._thread.start()

    def _pump(self) -> None:
        assert self.proc is not None and self.proc.stdout is not None
        for raw in self.proc.stdout:
            line = raw.rstrip("\n")
            if self.on_line:
                self.on_line(f"[{self.name}] {line}")
            if self.ready_marker and self.ready_marker in line:
                self._ready.set()

    def wait_ready(self, timeout: float) -> bool:
        """Wait for the ready marker. False if it times out or the process dies."""
        if self.ready_marker is None:
            return self.alive
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if self._ready.wait(0.1):
                return True
            if not self.alive:
                return False
        return self._ready.is_set()

    def send(self, text: str) -> None:
        if self.proc and self.proc.stdin:
            try:
                self.proc.stdin.write(text)
                self.proc.stdin.flush()
            except (OSError, ValueError):
                pass

    @property
    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def stop(self, timeout: float = 5.0) -> None:
        if not self.proc or self.proc.poll() is not None:
            return
        self._signal(signal.SIGTERM)
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self._signal(signal.SIGKILL)

    def _signal(self, sig) -> None:
        try:
            if os.name == "posix":
                os.killpg(os.getpgid(self.proc.pid), sig)  # type: ignore[union-attr]
            else:
                self.proc.terminate()  # type: ignore[union-attr]
        except (ProcessLookupError, OSError):
            pass
