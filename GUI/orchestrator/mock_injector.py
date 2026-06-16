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
    where each fault gains ``result`` (PASS = recovered within the ASIL deadline,
    FAIL = late or never). No reaction_ms / detected: the injector only compares
    the recovery time to the ASIL deadline (``delay_ms``) and reports the verdict,
  - streams ``progress`` + ``log`` lines on stdout so the live monitor moves
    while it runs (these are optional in the contract).

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
    machine = meta.get("machine", "?")
    cpu = meta.get("cpu", "?")
    firmware = meta.get("firmware", "firmware.elf")
    total = len(faults_in)

    emit({"type": "log", "message": f"[CONN]  Mock injector up (machine={machine}, cpu={cpu}, mode={meta.get('mode', '?')})"})
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

        # The injector compares recovery time to the ASIL deadline (delay_ms)
        # and reports PASS (recovered in time) or FAIL (late or never). It cannot
        # measure the exact reaction time, so none is sent.
        if rng.random() < 0.90:
            result, note = "PASS", None
        else:
            result, note = "FAIL", UNSAFE_TEXT

        entry = {
            "id": fid,
            "fault_type": ftype,
            "variable": var,
            "value": val,
            "min": f.get("min"),
            "max": f.get("max"),
            "result": result,
        }
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
            "machine": machine,
            "cpu": cpu,
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
