#!/usr/bin/env python3
"""
mock_injector.py
================
Stand-in for the C++ fault injector, speaking the real contract
(INJECTOR_INTERFACE_CONTRACT.md) so the whole pipeline runs end-to-end before
the binary or hardware exists. Point InjectorInterface at the compiled binary
(``injector_binary=...``) to switch to the real thing.

Contract behaviour:
  - reads the ``{meta, faults[]}`` input from ``--config``,
  - performs each fault (simulated) and writes ``{meta, faults[]}`` to ``--out``,
    where each fault gains ``result`` (PASS/FAIL) and, since this mock has a
    clock, ``reaction_ms`` + ``detected``,
  - streams ``progress`` + ``log`` lines on stdout so the live monitor moves
    while it runs (these are optional in the contract).

The orchestrator owns the final verdict: a PASS whose ``reaction_ms`` exceeds the
ASIL FTTI is still counted as a failure (the hazard window opened).

Usage:
    python mock_injector.py --config <in.json> --out <result.json> [--backend ...] [--gdb ...]
"""

import argparse
import json
import random
import sys
import time
import zlib

# FTTI (max allowed reaction time, ms) per ASIL — mirrors config.ASIL_FTTI_MS.
ASIL_FTTI_MS = {"ASIL-A": 50, "ASIL-B": 30, "ASIL-C": 20, "ASIL-D": 10}

UNSAFE_TEXT = "Faulty value used by controller — no safe-state transition (fault propagated)"
DETECTED_NO_REACTION = "Fault detected but controller held last command — no safe-state transition"


def emit(obj: dict) -> None:
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", required=True)
    ap.add_argument("--out", default="campaign_result.json")
    ap.add_argument("--backend", default="qemu")
    ap.add_argument("--gdb", default="")
    args = ap.parse_args()

    try:
        with open(args.config) as fh:
            cfg = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        emit({"type": "error", "message": f"could not read config: {exc}"})
        return 1

    meta = cfg.get("meta", {})
    faults_in = cfg.get("faults", [])
    asil = meta.get("asil_level", "ASIL-D")
    ftti = ASIL_FTTI_MS.get(asil, 10)
    micro = meta.get("micro", args.backend)
    firmware = meta.get("firmware", "firmware.elf")
    total = len(faults_in)

    emit({"type": "log", "message": f"[CONN]  Mock injector up (micro={micro}, mode={meta.get('mode', '?')})"})
    emit({"type": "log", "message": f"[INIT]  Firmware {firmware} on {meta.get('target', 'ARM')}, safety mechanisms active"})
    emit({"type": "log", "message": f"[INIT]  FTTI for {asil}: {ftti}ms · {total} injections"})

    faults_out = []
    for idx, f in enumerate(faults_in, start=1):
        fid = f.get("id", idx)
        var = f.get("variable") or f.get("address", "")
        val = f.get("value")
        ftype = f.get("fault_type", "")

        # Deterministic per-fault seed → reproducible runs (process-independent).
        seed = zlib.crc32(f"{ftype}|{var}|{val}|{asil}|{fid}".encode())
        rng = random.Random(seed)

        time.sleep(0.08)
        emit({"type": "log", "message": f"[TC-{fid:03d}] injecting value={val} into {var}"})

        detect_prob = 0.96
        react_prob = 0.97

        detected = rng.random() < detect_prob
        result = "FAIL"
        reaction_ms = None
        note = None
        if not detected:
            note = UNSAFE_TEXT
        elif rng.random() >= react_prob:
            note = DETECTED_NO_REACTION
        else:
            result = "PASS"
            if rng.random() < 0.88:
                reaction_ms = round(rng.uniform(1.5, ftti * 0.85), 1)
            else:
                reaction_ms = round(rng.uniform(ftti * 0.95, ftti * 1.6), 1)  # late → FAIL at the verdict

        entry = {
            "id": fid,
            "fault_type": ftype,
            "variable": var,
            "value": val,
            "min": f.get("min"),
            "max": f.get("max"),
            "result": result,
            "detected": detected,
        }
        if reaction_ms is not None:
            entry["reaction_ms"] = reaction_ms
        if note is not None:
            entry["note"] = note
        faults_out.append(entry)

        emit({"type": "progress", "current": idx, "total": total})

    out = {
        "meta": {
            "firmware": firmware,
            "mode": meta.get("mode", "HARDWARE"),
            "target": meta.get("target", "ARM"),
            "asil_level": asil,
            "micro": micro,
            "overhead_pct": round(random.Random(zlib.crc32(asil.encode())).uniform(2.0, 4.5), 1),
        },
        "faults": faults_out,
    }
    try:
        with open(args.out, "w") as fh:
            json.dump(out, fh, indent=2)
    except OSError as exc:
        emit({"type": "error", "message": f"could not write result file: {exc}"})
        return 1

    emit({"type": "log", "message": f"[OUT]  Wrote {len(faults_out)} results to {args.out}"})
    return 0


if __name__ == "__main__":
    sys.exit(main())
