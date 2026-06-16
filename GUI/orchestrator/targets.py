"""
targets.py
==========
The execution backend a fault is injected into, chosen from ``config.hardware``
("qemu" or "tivac"). The worker uses the common ``BaseTarget`` interface and
never branches on which one it got.

Phase 2: ``setup()`` brings up the debug server the injector talks to and gates
on readiness (its TCP port opening). There is NO separate GDB process — OpenOCD
itself is the debugger. The two background processes during a real run are:

    1. OpenOCD (Tiva) / QEMU (emulation)   ← brought up here
    2. the injector, main.cpp              ← launched by injector_interface

The injector connects to OpenOCD/QEMU directly over the selected port and does
the halt / inject / resume itself. ``teardown()`` stops the server (as a process
group). None of this runs in mock mode — the worker skips ``setup()`` for a
simulated campaign, so the demo never touches OpenOCD/QEMU.
"""

from __future__ import annotations

import os
import shutil
from abc import ABC, abstractmethod
from typing import Callable, List

from .proc import SupervisedProcess, wait_for_port

# Default firmware/ELF if the config carries none. The GUI config's ``firmware``
# field takes precedence; this env var is the fallback for headless runs.
FIRMWARE_PATH = os.environ.get("FI_FIRMWARE", "build/firmware.elf")


class TargetError(RuntimeError):
    """Raised when a backend cannot be brought up."""


class BaseTarget(ABC):
    name: str = "base"
    is_hardware: bool = False

    PORT = 0
    READY_TIMEOUT = float(os.environ.get("FI_READY_TIMEOUT", "20"))

    def __init__(self, config):
        self.config = config
        self._procs: List[SupervisedProcess] = []
        self._log: Callable[[str], None] = lambda m: None

    def set_log_callback(self, fn: Callable[[str], None]) -> None:
        """Route the server's (OpenOCD/QEMU) output to the GUI log."""
        self._log = fn or (lambda m: None)

    @abstractmethod
    def setup(self) -> None:
        """Bring the debug server up, gated on its port opening."""

    @abstractmethod
    def injector_args(self) -> List[str]:
        """Extra CLI args the injector needs to reach this target."""

    def _firmware(self) -> str:
        return getattr(self.config, "firmware", "") or FIRMWARE_PATH

    def teardown(self) -> None:
        """Stop the server (process group). Idempotent."""
        for p in reversed(self._procs):
            p.stop()
        self._procs = []


class QemuTarget(BaseTarget):
    name = "QEMU (lm3s6965evb)"
    is_hardware = False

    QEMU_BIN = os.environ.get("FI_QEMU", "qemu-system-arm")
    MACHINE = "lm3s6965evb"  # TI Stellaris Cortex-M3 — closest QEMU board to Tiva-C
    PORT = 1234              # QEMU gdbstub; the injector connects here

    def _qemu_cmd(self) -> List[str]:
        machine = getattr(self.config, "machine", "") or self.MACHINE
        cpu = getattr(self.config, "cpu", "")
        cmd = [self.QEMU_BIN, "-machine", machine]
        if cpu:
            cmd += ["-cpu", cpu]
        cmd += ["-kernel", self._firmware(), "-nographic",
                "-gdb", f"tcp::{self.PORT}", "-S"]  # freeze CPU; injector releases it
        return cmd

    def setup(self) -> None:
        if shutil.which(self.QEMU_BIN) is None:
            raise TargetError(
                f"{self.QEMU_BIN} not found on PATH. Install qemu-system-arm, "
                f"or run on the Tiva-C target."
            )
        self._log(f"[CONN]  Launching QEMU ({getattr(self.config, 'machine', '') or self.MACHINE}, "
                  f"cpu={getattr(self.config, 'cpu', '?')})...")
        qemu = SupervisedProcess("qemu", self._qemu_cmd(), on_line=self._log)
        self._procs.append(qemu)
        qemu.start()
        if not wait_for_port("localhost", self.PORT, self.READY_TIMEOUT):
            raise TargetError(f"QEMU gdbstub never opened on :{self.PORT}.")
        if not qemu.alive:
            raise TargetError("QEMU exited immediately — check the firmware/machine.")
        self._log(f"[CONN]  QEMU gdbstub up on :{self.PORT} — ready for the injector")

    def injector_args(self) -> List[str]:
        return ["--backend", "qemu", "--gdb", f"localhost:{self.PORT}"]


class TivaTarget(BaseTarget):
    name = "Tiva-C (TM4C123 via OpenOCD)"
    is_hardware = True

    OPENOCD_BIN = os.environ.get("FI_OPENOCD", "openocd")
    BOARD_CFG = os.environ.get("FI_BOARD_CFG", "board/ti_ek-tm4c123gxl.cfg")
    PORT = 3333          # OpenOCD gdb port; injector connects here (or telnet/tcl)
    TELNET_PORT = 4444
    TCL_PORT = 6666

    def _openocd_cmd(self) -> List[str]:
        return [self.OPENOCD_BIN, "-f", self.BOARD_CFG]

    def setup(self) -> None:
        if shutil.which(self.OPENOCD_BIN) is None:
            raise TargetError(
                f"{self.OPENOCD_BIN} not found on PATH. Install OpenOCD and "
                f"connect the Tiva-C LaunchPad over USB."
            )
        self._log("[CONN]  Launching OpenOCD (debugger)...")
        oocd = SupervisedProcess("openocd", self._openocd_cmd(), on_line=self._log)
        self._procs.append(oocd)
        oocd.start()
        if not wait_for_port("localhost", self.PORT, self.READY_TIMEOUT):
            raise TargetError(
                f"OpenOCD port :{self.PORT} never opened. Is the board plugged "
                f"in and the udev rule in place (no sudo needed)?"
            )
        if not oocd.alive:
            raise TargetError("OpenOCD exited immediately — check the board cfg / USB.")
        self._log(
            f"[CONN]  OpenOCD up (gdb :{self.PORT}, telnet :{self.TELNET_PORT}, "
            f"tcl :{self.TCL_PORT}) — ready for the injector"
        )

    def injector_args(self) -> List[str]:
        # The injector dials this endpoint. The actual port is user-selected in
        # the UI (config.gdb_port) and injected by injector_interface; this is
        # only the default if none is chosen.
        return ["--backend", "tiva", "--gdb", f"localhost:{self.PORT}"]


class TargetFactory:
    @staticmethod
    def create(config) -> BaseTarget:
        hw = (getattr(config, "hardware", "") or "").lower()
        if hw in ("qemu", "emulation"):
            return QemuTarget(config)
        if hw in ("tivac", "tiva", "tiva-c", "tiva_c"):
            return TivaTarget(config)
        raise TargetError(f"Unknown hardware mode: {config.hardware!r}")
