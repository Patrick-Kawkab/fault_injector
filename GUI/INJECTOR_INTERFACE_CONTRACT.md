# Fault Injection Interface Contract

**Orchestrator (Python/GUI) ⇆ Injector (C++)** · v0.1 · draft for review

This document defines the two JSON interfaces between the orchestrator and the
injector, plus how the orchestrator launches the injector. Both sides build
against this contract.

- **Input** — `config.json`, written by the orchestrator, **read by the injector**. (This is the "our format is your input" part.)
- **Output** — `result.json`, written by the injector, **read by the orchestrator**.
- **Invocation** — the command line and process behaviour the orchestrator relies on.

The two payloads are deliberately symmetric: the output is the input with an
`id` and a `result` added per fault. Read the list, inject each entry, write the
list back with the verdict filled in.

---

## 1. Invocation

The orchestrator launches the injector as a child process, headless (no
terminal), after OpenOCD and GDB are already up:

```
<injector> --config <config.json> --out <result.json> --backend <tiva|qemu> --gdb <host:port>
```

| Arg | Meaning |
| --- | --- |
| `--config` | Path to the input JSON the injector must read. |
| `--out` | Path the injector must write the result JSON to. |
| `--backend` | `tiva` (real hardware) or `qemu` (emulation). |
| `--gdb` | Endpoint the injector dials to reach the debug stack, e.g. `localhost:3333`. |

Process behaviour the orchestrator depends on:

- **stdout** — optional progress lines for the live monitor (see §4). If you emit nothing, the monitor simply fills in once at the end.
- **stderr** — free‑form diagnostics. The orchestrator surfaces these as log lines; they do not need to be JSON.
- **Exit code** — `0` means the campaign ran to completion. A fault whose `result` is `FAIL` is a **normal, successful outcome** (the injector did its job) and must **not** cause a non‑zero exit. Reserve non‑zero exit codes for infrastructure failures only (cannot connect to the port, bad config, target unreachable).
- The injector reads `--config`, performs every fault in order, writes `--out`, then exits.

---

## 2. Input — orchestrator → injector

```json
{
  "meta": {
    "firmware": "tiva_led.elf",
    "mode": "HARDWARE",
    "target": "ARM",
    "asil_level": "ASIL-D",
    "backend": "tiva",
    "gdb": "localhost:3333"
  },
  "faults": [
    {
      "id": 1,
      "fault_type": "sensor_corruption",
      "variable": "speed_RPM",
      "address": "",
      "system_state": "cruise_active",
      "value": 30,
      "min": 0,
      "max": 2,
      "duration_ms": 3500,
      "interval_ms": 50,
      "delay_ms": 0,
      "bit_position": 0
    }
  ]
}
```

### `meta` (campaign‑level, applies to every fault)

| Field | Type | Meaning |
| --- | --- | --- |
| `firmware` | string | ELF the target is running. |
| `mode` | string | `HARDWARE` (tiva) or `EMULATION` (qemu). |
| `target` | string | Architecture, currently `ARM`. |
| `asil_level` | string | `ASIL-A` … `ASIL-D`. Drives the orchestrator's pass/fail thresholds, not the injection. |
| `backend` | string | Mirror of `--backend`. |
| `gdb` | string | Mirror of `--gdb`. |

### `faults[]` (one object per injection)

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | int | Unique injection id (1‑based). |
| `fault_type` | string | One of `sensor_corruption`, `memory_corruption`, `bit_flip`, `pc_error`, `task_delay`. |
| `variable` | string | Variable to inject into, e.g. `speed_RPM`. **Empty for `pc_error`.** The injector resolves it to an address via the ELF symbol table / GDB. |
| `address` | string | Hex address for `pc_error` (the program counter has no symbol), e.g. `0x00000400`. **Empty for all other types.** Always present so the shape stays invariant. |
| `system_state` | string | Variable to monitor to judge whether the system reached its safe state. Only meaningful for `sensor_corruption` / `task_delay`, but **always present** (empty otherwise) so the JSON shape is invariant across configs. |
| `value` | int | Value written for corruption faults. **For `task_delay` this is the delay magnitude in ms.** Ignored for `bit_flip` / `pc_error`. |
| `min` | int | Low end of the variable's valid range (for context / verdict). |
| `max` | int | High end of the valid range. |
| `duration_ms` | int | How long to keep injecting the fault, in ms. Must exceed the firmware's own fault‑reaction threshold or the system never reacts (see sizing note). |
| `interval_ms` | int | How often to re‑inject during the hold window, in ms. Must be smaller than the firmware's sampling period so re‑injection beats any ISR that overwrites the value. |
| `delay_ms` | int | Wait this long after start before injecting. `0` = immediate. |
| `bit_position` | int | Bit to flip when `fault_type == "bit_flip"`; ignored otherwise. |

> **Per‑type field use** (the parser keys off `fault_type`): `sensor_corruption` → `variable` + `value` + `system_state`; `memory_corruption` → `variable` + `value`; `bit_flip` → `variable` + `bit_position`; `pc_error` → `address` only; `task_delay` → `variable` + `value` (= delay ms) + `system_state`. Unused fields are still present, just empty/zero.

> **Sizing `duration_ms` / `interval_ms` (firmware‑specific).** These must be
> matched to the firmware under test, not guessed. `duration_ms` has to outlast
> the firmware's own fault‑reaction logic — e.g. if the cancel path needs N
> consecutive bad samples at a sampling period P, the hold must exceed `N × P`
> (give margin on top). `interval_ms` has to be shorter than that sampling
> period P so the injected value is re‑applied before the task samples it, even
> when an ISR is concurrently writing the real value. Example: a cancel path of
> 30 samples at 100 ms needs `duration_ms > 3000` (use ~3500) and
> `interval_ms ≈ 50` (re‑inject twice per 100 ms window).

> Field‑name note: these map 1:1 to the orchestrator's internal config
> (`min` ← `min_value`, `max` ← `max_value`, `value` ← `fault_value`). The
> orchestrator renames them on the way out so your input is clean.

---

## 3. Output — injector → orchestrator

Same shape as the input, with `id` + `result` guaranteed on every fault.

```json
{
  "meta": {
    "firmware": "tiva_led.elf",
    "mode": "HARDWARE",
    "target": "ARM",
    "asil_level": "ASIL-D",
    "overhead_pct": 3.2
  },
  "faults": [
    {
      "id": 1,
      "fault_type": "sensor_corruption",
      "variable": "speed_RPM",
      "value": 30,
      "min": 0,
      "max": 2,
      "result": "FAIL",
      "reaction_ms": 7.4,
      "detected": true,
      "note": "cruise control did not disengage"
    }
  ]
}
```

### Required per fault

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | int | Matches the input `id`. |
| `result` | string | Verdict — one of `PASS` / `FAIL` / `ERROR`. See semantics below. |

Echoing the input fields (`fault_type`, `variable`, `value`, `min`, `max`) is
recommended so each result row is self‑describing.

### `result` semantics — read this carefully

| Value | Meaning | Counts as |
| --- | --- | --- |
| `PASS` | The system **handled** the fault and reached its safe state (e.g. cruise control disengaged). The safe / intended outcome. | success |
| `FAIL` | The fault **propagated** — the system did not mitigate it (unsafe). This is the dangerous case the campaign exists to catch. | failure |
| `ERROR` | The injection could **not be performed** (bad address, target lost). Excluded from pass/fail statistics. | neither |

This is the one place ambiguity would invert the entire report, so it is fixed
by the contract: **`PASS` = system safe, `FAIL` = system unsafe.** Your current
`"FAILED"` should map to `FAIL` only if it means "the system did not handle the
fault." If your `FAILED` currently means something else, flag it — do not silently map it.

### Optional per fault (enables the richer verdict)

| Field | Type | Effect if present |
| --- | --- | --- |
| `reaction_ms` | number | Time to reach the safe state. Restores the FTTI timing criterion and the reaction‑time chart. Without it, those are hidden. |
| `detected` | bool | Whether the safety mechanism flagged the fault before reacting. |
| `note` | string | Free text shown verbatim in the results table's "system response" column. |

### Optional `meta`

| Field | Type | Meaning |
| --- | --- | --- |
| `overhead_pct` | number | Measured runtime overhead of the safety mechanism (%). Feeds the overhead criterion. |

---

## 4. Optional live progress (stdout)

If you want the GUI's Live Monitor to tick per injection instead of filling in
at the end, print one line of JSON per event to stdout as you go:

```
{"type": "progress", "id": 1, "total": 30}
{"type": "log", "message": "halting target, writing 0x20000008"}
```

These are optional and independent of the `--out` file, which remains the
source of truth.

---

## 5. Deltas from your current output

Your sample:

```json
{ "faults": [ { "Firmware": "tiva_led.elf", "Mode": "HARDWARE", "Target": "ARM",
                "address": "0x20000008", "fault_type": "memory_corruption",
                "id": 1, "max": 2, "min": 0, "result": "FAILED", "value": 255 } ] }
```

To conform:

1. Lower‑case the three capitalised keys (`Firmware` → `firmware`, `Mode` → `mode`, `Target` → `target`) and move them into a top‑level `meta` block.
2. Change `result` values to the `PASS` / `FAIL` / `ERROR` enum (your `FAILED` → `FAIL`, assuming it means "system did not handle it").
3. Carry `variable` (the variable name) instead of a hex `address`; everything else (`id`, `fault_type`, `value`, `min`, `max`) already matches.

Optional but recommended: add `reaction_ms` per fault and `overhead_pct` in
`meta` so the ISO 26262 verdict shows timing, not just a pass/fail count.

---

## 6. What changes on the orchestrator side (for our reference)

- Add `firmware` to `FaultConfig` and derive `mode`/`target` from the backend.
- Add an input serializer that emits the §2 shape (renaming `min_value`/`max_value`/`fault_value` → `min`/`max`/`value`).
- Add an output reader that parses the §3 shape, maps `result` → outcome, and degrades the timing UI gracefully when `reaction_ms` is absent.
- The mock injector stays the default fallback, so the demo runs with no hardware.
