"""
results_manager.py
==================
Turns the per-fault results from the injector (INJECTOR_INTERFACE_CONTRACT.md)
into the ``finished`` payload the GUI's Results and Report tabs expect.

The injector reports a functional ``result`` per fault:
    PASS  — system reached its safe state (handled the fault)
    FAIL  — fault propagated (system did not mitigate)
    ERROR — injection could not be performed (excluded from pass/fail stats)

The injector enforces the ASIL recovery deadline itself (a late recovery is
reported as FAIL), so the orchestrator does not re-gate on timing. The verdict
rests on the handling rate and the runtime overhead. No reaction_ms / detected
are expected from the injector.

New keys describe fault handling; legacy keys (detected, coverage_pct,
avg_latency_ms, ...) are kept as aliases so older code paths still work.
"""

from __future__ import annotations

from typing import List

from config import (
    FAULT_TYPES, ASIL_COVERAGE, ASIL_FTTI_MS, MAX_OVERHEAD_PCT,
    expected_safe_state, UNSAFE_OUTCOME_TEXT,
)


class ResultsManager:
    def __init__(self, config):
        self.config = config
        self.cases: List[dict] = []
        self.overhead_pct: float = 0.0
        self._ftti = ASIL_FTTI_MS.get(config.asil_level, 10)

    def add_result(self, msg: dict) -> dict:
        res = str(msg.get("result", "")).upper()
        react = msg.get("reaction_ms")
        if react is not None:
            react = round(float(react), 1)
        detected = msg.get("detected")
        iid = int(msg.get("id", len(self.cases) + 1))
        ft_raw = msg.get("fault_type", self.config.fault_type)
        note = msg.get("note")
        expected = expected_safe_state(self.config.sensor, self.config.fault_type)

        if res == "ERROR":
            outcome = "Error"
            reason = note or "Injection could not be performed"
            response = note or "Injection error — fault not applied"
        elif res == "PASS":
            # The injector already enforced the ASIL deadline (late -> FAIL),
            # so a reported PASS is a pass.
            outcome, reason = "Pass", ""
            response = note or expected
        else:  # FAIL or anything unexpected
            outcome = "Fail"
            reason = note or "Fault propagated — system did not reach safe state"
            response = note or UNSAFE_OUTCOME_TEXT

        case = {
            "id": f"TC-{iid:03d}",
            "fault_type": FAULT_TYPES.get(ft_raw, ft_raw),
            "variable": msg.get("variable") or msg.get("address") or self.config.variable or self.config.address,
            "value": msg.get("value"),
            "min": msg.get("min"),
            "max": msg.get("max"),
            "injected": outcome != "Error",
            "detected": detected,
            "system_response": response,
            "expected_response": expected,
            "reaction_ms": react,
            "fail_reason": reason,
            "outcome": outcome,
            # ── legacy aliases (older GUI/report code paths) ──
            "safe_state_reached": outcome == "Pass",
            "latency_ms": react if outcome == "Pass" else None,
            "result": outcome,
        }
        self.cases.append(case)
        return case

    def set_overhead(self, value) -> None:
        if value is not None:
            self.overhead_pct = round(float(value), 1)

    # -- metrics ------------------------------------------------------------
    @property
    def total(self) -> int:
        return len(self.cases)

    @property
    def errors(self) -> int:
        return sum(1 for c in self.cases if c["outcome"] == "Error")

    @property
    def handled(self) -> int:
        """Injections the system mitigated correctly (safe state within FTTI)."""
        return sum(1 for c in self.cases if c["outcome"] == "Pass")

    @property
    def decided(self) -> int:
        """Injections that produced a real verdict (excludes ERROR)."""
        return self.total - self.errors

    @property
    def handling_pct(self) -> float:
        return round(self.handled / self.decided * 100, 1) if self.decided else 0.0

    # -- payload ------------------------------------------------------------
    def build_payload(self) -> dict:
        reactions = [c["reaction_ms"] for c in self.cases
                     if c["outcome"] == "Pass" and c["reaction_ms"] is not None]
        has_timing = any(c["reaction_ms"] is not None for c in self.cases)
        avg = round(sum(reactions) / len(reactions), 1) if reactions else 0
        max_r = round(max(reactions), 1) if reactions else 0
        min_r = round(min(reactions), 1) if reactions else 0

        handling = self.handling_pct
        overhead = self.overhead_pct
        asil = self.config.asil_level
        rate_target = ASIL_COVERAGE.get(asil, 90)
        ftti = ASIL_FTTI_MS.get(asil, 10)

        rate_pass = handling >= rate_target
        oh_pass = overhead <= MAX_OVERHEAD_PCT
        criteria = [("Fault-handling rate", f"{handling}%", f"\u2265 {rate_target}%", rate_pass)]

        ftti_pass = True
        if has_timing:
            ftti_pass = (max_r <= ftti) if max_r else True
            criteria.append(("Reaction time (FTTI)", f"{max_r}ms", f"\u2264 {ftti}ms", ftti_pass))
        criteria.append(("Performance overhead", f"{overhead}%", f"\u2264 {MAX_OVERHEAD_PCT}%", oh_pass))

        return {
            "config": self.config,
            "test_cases": self.cases,
            "total": self.total,
            # ── new primary metrics ──
            "handled": self.handled,
            "errors": self.errors,
            "handling_pct": handling,
            "has_timing": has_timing,
            "avg_reaction_ms": avg,
            "max_reaction_ms": max_r,
            "min_reaction_ms": min_r,
            "ftti_ms": ftti,  # ASIL deadline (ms) sent to the injector
            # ── legacy aliases (kept so existing tabs keep rendering) ──
            "detected": self.handled,
            "coverage_pct": handling,
            "avg_latency_ms": avg,
            "max_latency_ms": max_r,
            "min_latency_ms": min_r,
            "overhead_pct": overhead,
            "criteria": criteria,
            "passed_asil": rate_pass and ftti_pass and oh_pass,
        }
