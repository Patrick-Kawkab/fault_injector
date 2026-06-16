"""
config.py
---------
Shared FaultConfig dataclass.
This is the contract between the GUI and the orchestrator.
Neither side should modify field names without telling the other.
"""

from dataclasses import dataclass, asdict, field
from typing import List, Optional
import json


@dataclass
class FaultConfig:
    hardware: str = "tivac"          # "tivac" or "qemu"
    firmware: str = "tiva_led.elf"   # ELF image running on the target
    machine: str = "lm3s6965evb"     # QEMU machine / target board (meta)
    cpu: str = "cortex-m4"           # CPU core, e.g. cortex-m4 / cortex-m3 (meta)
    gdb_port: int = 3333             # port the injector dials to reach the debug stack (meta gdb)
    sensor: str = ""                  # "WSS", "TPS", "MAP", "ECT"
    fault_type: str = ""              # see FAULT_TYPES: sensor_corruption, memory_corruption, bit_flip, pc_error, task_delay
    variable: str = ""                # variable to inject into, e.g. "speed_RPM" (empty for pc_error)
    address: str = ""                 # hex address, e.g. "0x00000400" (pc_error only; empty otherwise)
    system_state: str = ""            # variable to monitor for safety (sensor_corruption / task_delay only)
    duration_s: int = 60              # seconds (campaign-level, informational)
    num_faults: int = 30              # number of injections in the campaign
    duration_ms: int = 3500           # hold the fault this long per injection (ms)
    interval_ms: int = 50             # re-inject the fault every this many ms
    delay_ms: int = 0                 # ASIL recovery deadline (ms); set from asil_level at serialization
    min_value: int = 0                # sensor min
    max_value: int = 255              # sensor max
    fault_value: int = 0              # value to inject (corruption); delay magnitude (ms) for task_delay
    bit_position: int = 0             # bit to flip (for bit_flip mode)
    asil_level: str = "ASIL-D"       # "ASIL-A", "ASIL-B", "ASIL-C", "ASIL-D"
    expected_behavior: str = ""       # human description of expected system response
    varied_faults: Optional[List[dict]] = field(default=None)  # list of fault_payload() dicts; repeated to num_faults

    def to_json(self) -> str:
        return json.dumps(asdict(self), indent=2)

    def to_file(self, path: str) -> None:
        with open(path, "w") as f:
            f.write(self.to_json())

    def _mode(self) -> str:
        return "HARDWARE" if self.hardware == "tivac" else "EMULATION"

    def to_injector_input(self, count: int = None) -> dict:
        """Build the ``{meta, faults[]}`` contract the injector reads.

        See INJECTOR_INTERFACE_CONTRACT.md. ``count`` (default: ``num_faults``)
        identical injections are emitted (id 1..count).
        """
        if count is None:
            count = self.num_faults
        meta = {
            "firmware": self.firmware,
            "mode": self._mode(),
            "target": "ARM",
            "asil_level": self.asil_level,
            "machine": self.machine,
            "cpu": self.cpu,
            "gdb": f"localhost:{self.gdb_port}",
        }
        # delay_ms carries the ASIL-specific recovery deadline the injector
        # compares against (late recovery -> FAIL). Same for every fault in the
        # campaign. This is NOT the task_delay magnitude (that lives in value).
        deadline = ASIL_FTTI_MS.get(self.asil_level, 0)
        defs = self.varied_faults if self.varied_faults else [self.fault_payload()]
        faults = [
            {"id": i, **defs[(i - 1) % len(defs)], "delay_ms": deadline}
            for i in range(1, count + 1)
        ]
        return {"meta": meta, "faults": faults}

    def fault_payload(self) -> dict:
        """The per-fault dict (without id), in contract field order."""
        return {
            "fault_type": self.fault_type,
            "variable": self.variable,
            "address": self.address,
            "system_state": self.system_state,
            "value": self.fault_value,
            "min": self.min_value,
            "max": self.max_value,
            "duration_ms": self.duration_ms,
            "interval_ms": self.interval_ms,
            "delay_ms": self.delay_ms,
            "bit_position": self.bit_position,
        }

    @staticmethod
    def from_json(data: str) -> "FaultConfig":
        return FaultConfig(**json.loads(data))

    def is_valid(self) -> tuple[bool, str]:
        """Returns (is_valid, error_message)"""
        if not self.sensor:
            return False, "Sensor is required."
        if not self.fault_type:
            return False, "Fault type is required."
        if self.fault_type == "pc_error":
            if not self.address:
                return False, "Address is required for PC error."
        elif not self.variable:
            return False, "Variable name is required."
        if self.duration_s <= 0:
            return False, "Duration must be greater than 0."
        if self.duration_ms <= 0 or self.interval_ms <= 0:
            return False, "Hold time and re-inject interval must be greater than 0."
        if self.interval_ms >= self.duration_ms:
            return False, "Re-inject interval must be smaller than the hold time."
        if self.min_value >= self.max_value:
            return False, "Min value must be less than max value."
        return True, ""


# Sensor knowledge base — AI or GUI uses this to auto-fill sensor details
SENSOR_DB = {
    "WSS": {
        "name": "Wheel Speed Sensor",
        "min_value": 0,
        "max_value": 255,
        "asil_level": "ASIL-D",
        "unit": "km/h",
    },
    "TPS": {
        "name": "Throttle Position Sensor",
        "min_value": 0,
        "max_value": 100,
        "asil_level": "ASIL-C",
        "unit": "%",
    },
    "MAP": {
        "name": "Manifold Absolute Pressure",
        "min_value": 10,
        "max_value": 110,
        "asil_level": "ASIL-B",
        "unit": "kPa",
    },
    "ECT": {
        "name": "Engine Coolant Temperature",
        "min_value": -40,
        "max_value": 130,
        "asil_level": "ASIL-B",
        "unit": "°C",
    },
    "APS": {
        "name": "Accelerator Pedal Sensor",
        "min_value": 0,
        "max_value": 100,
        "asil_level": "ASIL-D",
        "unit": "%",
    },
    "BPS": {
        "name": "Brake Pressure Sensor",
        "min_value": 0,
        "max_value": 200,
        "asil_level": "ASIL-D",
        "unit": "bar",
    },
}

GDB_PORTS = [3333, 4444, 1234, 9001]
MACHINES  = ["lm3s6965evb"]
CPUS      = ["cortex-m4", "cortex-m3"]

FAULT_TYPES = {
    "sensor_corruption": "Sensor Corruption",
    "memory_corruption": "Memory Corruption",
    "bit_flip":          "Bit Flip",
    "pc_error":          "PC Error",
    "task_delay":        "Task Delay",
}

ASIL_LEVELS = ["ASIL-A", "ASIL-B", "ASIL-C", "ASIL-D"]

# Minimum fault-handling (safe-state) rate target (%) per ASIL level — thesis Section 2.1.6.3.
# Interpreted now as: of all injected faults, the fraction the system must successfully
# mitigate (transition to the safe state) to satisfy the ASIL requirement.
ASIL_COVERAGE = {"ASIL-A": 60, "ASIL-B": 70, "ASIL-C": 80, "ASIL-D": 90}

# Fault-Tolerant Time Interval (FTTI) per ASIL, in ms — the maximum time the system may
# take to recover. This value is sent to the injector as delay_ms; the injector FAILs any
# injection that recovers later than this, so a late recovery counts as a failure.
ASIL_FTTI_MS = {"ASIL-A": 50, "ASIL-B": 30, "ASIL-C": 20, "ASIL-D": 10}
# Backwards-compatible alias (older modules referenced this name).
ASIL_MAX_LATENCY_MS = ASIL_FTTI_MS

# Maximum acceptable runtime overhead (%) — thesis performance target
MAX_OVERHEAD_PCT = 5

# Number of injections per campaign (the orchestrator expands the configured
# fault into this many entries in the injector input).
CAMPAIGN_SIZE = 30

HARDWARE_MODES = {"tivac": "Tiva-C (Bare Metal)", "qemu": "QEMU (Emulation)"}


# ── Safe-state knowledge base ────────────────────────────────────────────────
# For each sensor: what the System Under Test (the cruise-control / powertrain
# application) MUST do when a fault on that sensor is injected. This is the
# "expected behaviour" the campaign verifies. ``per_fault`` overrides the default
# ``safe_state`` text for specific fault types where the reaction differs.
#
# A campaign PASSES an injection only if the observed system response matches this
# expected safe state AND it is reached within the ASIL FTTI.
SAFE_STATE_DB = {
    "WSS": {
        "safe_state": "Cruise control DISENGAGED, driver warning raised, revert to manual throttle",
        "per_fault": {
            "sensor_corruption": "Cruise control DISENGAGED (implausible wheel speed), driver warning raised",
        },
    },
    "TPS": {
        "safe_state": "Torque request limited, limp-home mode entered, cruise control inhibited",
        "per_fault": {
            "sensor_corruption": "Torque clamped to idle, limp-home mode, cruise control inhibited",
        },
    },
    "APS": {
        "safe_state": "Pedal signal rejected, fail-safe idle / limp-home, cruise control DISENGAGED",
    },
    "BPS": {
        "safe_state": "Cruise control DISENGAGED immediately, fall back to manual braking",
    },
    "MAP": {
        "safe_state": "Switch to speed-density model (substitute value), MIL set",
    },
    "ECT": {
        "safe_state": "Substitute default coolant temp, force cooling fan ON, MIL set",
    },
}

# Generic fallback when a sensor isn't in SAFE_STATE_DB.
_GENERIC_SAFE_STATE = "Faulty signal rejected, controller enters fail-safe / limp-home, hazard function disabled"

# What the system does when it FAILS to handle the fault (for logs/reports).
UNSAFE_OUTCOME_TEXT = "Faulty value used by controller — no safe-state transition (fault propagated)"


def expected_safe_state(sensor: str, fault_type: str) -> str:
    """Return the expected safe-state behaviour for a (sensor, fault_type) pair."""
    entry = SAFE_STATE_DB.get((sensor or "").upper())
    if not entry:
        return _GENERIC_SAFE_STATE
    per_fault = entry.get("per_fault", {})
    return per_fault.get(fault_type, entry["safe_state"])
