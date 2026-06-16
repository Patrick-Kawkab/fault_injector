"""
targets.py
==========
The execution backend a fault is injected into, chosen from ``config.hardware``
("qemu" or "tivac"). The worker uses the common ``BaseTarget`` interface and
never branches on which one it got.

Phase 2: ``setup()`` brings up the headless debug stack the real injector needs
and gates each stage on readiness:

    QEMU/OpenOCD  ──(TCP port open)──>  GDB attached (GDB_READY)  ──>  injector

``teardown()`` stops everything in reverse order, as process groups. None of
this runs in mock mode — the worker skips ``setup()`` for a simulated campaign,
so the demo never touches OpenOCD/GDB/QEMU.
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

    GDB_BIN = os.environ.get("FI_GDB", "arm-none-eabi-gdb")
    GDB_PORT = 0
    READY_TIMEOUT = float(os.environ.get("FI_READY_TIMEOUT", "20"))

    def __init__(self, config):
        self.config = config
        self._procs: List[SupervisedProcess] = []
        self._log: Callable[[str], None] = lambda m: None

    def set_log_callback(self, fn: Callable[[str], None]) -> None:
        """Route OpenOCD/GDB/QEMU output to the GUI log."""
        self._log = fn or (lambda m: None)

    @abstractmethod
    def setup(self) -> None:
        """Bring the backend + debug stack up, gated on readiness."""

    @abstractmethod
    def injector_args(self) -> List[str]:
        """Extra CLI args the injector needs to reach this target."""

    # -- shared helpers -----------------------------------------------------
    def _firmware(self) -> str:
        return getattr(self.config, "firmware", "") or FIRMWARE_PATH

    def _gdb_connect_cmds(self) -> List[str]:
        """Backend-specific -ex commands run after connecting (overridable)."""
        return []

    def _gdb_cmd(self) -> List[str]:
        cmd = [
            self.GDB_BIN, "--nx", "-q",
            "-ex", "set pagination off",
            "-ex", "set confirm off",
            "-ex", f"target extended-remote localhost:{self.GDB_PORT}",
        ]
        for c in self._gdb_connect_cmds():
            cmd += ["-ex", c]
        cmd += ["-ex", "echo GDB_READY\\n", self._firmware()]
        return cmd

    def _start_gdb(self) -> None:
        """Launch GDB headless, attach to the server, wait for GDB_READY."""
        if shutil.which(self.GDB_BIN) is None:
            raise TargetError(
                f"{self.GDB_BIN} not found on PATH. Install the ARM GDB "
                f"(arm-none-eabi-gdb / gdb-multiarch)."
            )
        gdb = SupervisedProcess("gdb", self._gdb_cmd(), on_line=self._log,
                                ready_marker="GDB_READY")
        self._procs.append(gdb)
        self._log(f"[CONN]  GDB attaching to localhost:{self.GDB_PORT}...")
        gdb.start()
        if not gdb.wait_ready(self.READY_TIMEOUT):
            raise TargetError(
                "GDB did not reach the target (no GDB_READY). Check the debug "
                "server and that the firmware ELF path is correct."
            )
        self._log("[CONN]  GDB attached, target halted")

    def teardown(self) -> None:
        """Stop the whole stack in reverse order. Idempotent."""
        for p in reversed(self._procs):
            p.stop()
        self._procs = []


class QemuTarget(BaseTarget):
    name = "QEMU (lm3s6965evb)"
    is_hardware = False

    QEMU_BIN = os.environ.get("FI_QEMU", "qemu-system-arm")
    MACHINE = "lm3s6965evb"  # TI Stellaris Cortex-M3 — closest QEMU board to Tiva-C
    GDB_PORT = 1234

    def _qemu_cmd(self) -> List[str]:
        return [
            self.QEMU_BIN, "-machine", self.MACHINE,
            "-kernel", self._firmware(), "-nographic",
            "-gdb", f"tcp::{self.GDB_PORT}", "-S",  # freeze CPU; GDB releases it
        ]

    def setup(self) -> None:
        if shutil.which(self.QEMU_BIN) is None:
            raise TargetError(
                f"{self.QEMU_BIN} not found on PATH. Install qemu-system-arm, "
                f"or run on the Tiva-C target."
            )
        self._log(f"[CONN]  Launching QEMU ({self.MACHINE})...")
        qemu = SupervisedProcess("qemu", self._qemu_cmd(), on_line=self._log)
        self._procs.append(qemu)
        qemu.start()
        if not wait_for_port("localhost", self.GDB_PORT, self.READY_TIMEOUT):
            raise TargetError(f"QEMU gdbstub never opened on :{self.GDB_PORT}.")
        if not qemu.alive:
            raise TargetError("QEMU exited immediately — check the firmware/machine.")
        self._log(f"[CONN]  QEMU gdbstub up on :{self.GDB_PORT}")
        self._start_gdb()

    def injector_args(self) -> List[str]:
        return ["--backend", "qemu", "--gdb", f"localhost:{self.GDB_PORT}"]


class TivaTarget(BaseTarget):
    name = "Tiva-C (TM4C123 via OpenOCD)"
    is_hardware = True

    OPENOCD_BIN = os.environ.get("FI_OPENOCD", "openocd")
    BOARD_CFG = os.environ.get("FI_BOARD_CFG", "board/ti_ek-tm4c123gxl.cfg")
    GDB_PORT = 3333
    TELNET_PORT = 4444
    TCL_PORT = 6666

    def _openocd_cmd(self) -> List[str]:
        return [self.OPENOCD_BIN, "-f", self.BOARD_CFG]

    def _gdb_connect_cmds(self) -> List[str]:
        return ["monitor reset halt"]

    def setup(self) -> None:
        if shutil.which(self.OPENOCD_BIN) is None:
            raise TargetError(
                f"{self.OPENOCD_BIN} not found on PATH. Install OpenOCD and "
                f"connect the Tiva-C LaunchPad over USB."
            )
        self._log("[CONN]  Launching OpenOCD...")
        oocd = SupervisedProcess("openocd", self._openocd_cmd(), on_line=self._log)
        self._procs.append(oocd)
        oocd.start()
        if not wait_for_port("localhost", self.GDB_PORT, self.READY_TIMEOUT):
            raise TargetError(
                f"OpenOCD gdb port :{self.GDB_PORT} never opened. Is the board "
                f"plugged in and the udev rule in place (no sudo needed)?"
            )
        if not oocd.alive:
            raise TargetError("OpenOCD exited immediately — check the board cfg / USB.")
        self._log(
            f"[CONN]  OpenOCD up (gdb :{self.GDB_PORT}, "
            f"telnet :{self.TELNET_PORT}, tcl :{self.TCL_PORT})"
        )
        self._start_gdb()

    def injector_args(self) -> List[str]:
        # The injector dials this endpoint. GDB owns :3333, so if the injector
        # talks to OpenOCD directly, switch this to the telnet (4444) or
        # tcl-rpc (6666) port once the C++ team confirms which one it uses.
        return ["--backend", "tiva", "--gdb", f"localhost:{self.GDB_PORT}"]


class TargetFactory:
    @staticmethod
    def create(config) -> BaseTarget:
        hw = (getattr(config, "hardware", "") or "").lower()
        if hw in ("qemu", "emulation"):
            return QemuTarget(config)
        if hw in ("tivac", "tiva", "tiva-c", "tiva_c"):
            return TivaTarget(config)
        raise TargetError(f"Unknown hardware mode: {config.hardware!r}")
