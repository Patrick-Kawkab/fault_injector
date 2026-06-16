"""
chat_assistant.py
-----------------
Framework assistant. Answers questions about the framework — its features,
step-by-step how-to instructions, the sensors / fault types / ISO 26262 rules,
and the latest campaign results — using Google's free Gemini API through its
OpenAI-compatible endpoint.

Setup (one-time, free):
    1. pip install openai
    2. Get a free API key from https://aistudio.google.com/apikey
    3. Set it in the SAME terminal you launch the app from. PowerShell:
           $env:GEMINI_API_KEY="your-key-here"
           python main.py
       (If you launch from VS Code, set it in that integrated terminal or in
        launch.json — otherwise the app won't see the key.)

The model is told to answer ONLY from the knowledge base below plus the campaign
results it is given, and to say "the framework doesn't have that / I don't know"
when a question falls outside that scope — so it doesn't invent menus or steps.
"""

import os
from PyQt5.QtCore import QThread, pyqtSignal

# Free Gemini via its OpenAI-compatible endpoint. If Google changes the free
# tier, just swap the model name here (e.g. "gemini-2.0-flash").
MODEL    = "gemini-2.5-flash-lite"
BASE_URL = "https://generativelanguage.googleapis.com/v1beta/openai/"


def _load_reference() -> str:
    """Optionally load extra project reference text (e.g. thesis excerpts) so the
    assistant can answer detailed, project-specific questions. Drop a plain-text
    file named 'thesis_reference.txt' next to the app and it is used automatically;
    if it's absent the assistant still works fine without it."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "thesis_reference.txt")
    try:
        with open(path, encoding="utf-8") as f:
            return f.read().strip()[:400000]   # generous cap (Gemini handles it)
    except Exception:
        return ""


REFERENCE = _load_reference()


FRAMEWORK_KB = """
OVERVIEW
The Fault Injection Framework is a PyQt5 desktop tool for ISO 26262 automotive
functional-safety testing. It injects a configured fault into a target's sensor signal or memory and
verifies whether the System Under Test REACTS CORRECTLY - i.e. whether the
safety mechanism transitions the system to its expected SAFE STATE (for example,
disengaging cruise control when the wheel-speed sensor reads an implausible 0),
and whether it does so within the Fault-Tolerant Time Interval (FTTI). It then
reports a fault-handling (mitigation) rate and an ISO 26262 pass/fail verdict.

ARCHITECTURE
- GUI (this app) -> OrchestratorWorker (background thread) -> InjectorInterface
  -> injector -> target.
- The injector is currently a MOCK (mock_injector.py) that simulates the
  system's reaction to each fault, so the whole app runs without any hardware. A real C++ injector replaces it
  later by pointing OrchestratorWorker at the compiled binary.
- Target backends: "qemu" (QEMU emulation of a Cortex-M board) or "tivac" (a real
  Tiva-C TM4C123 LaunchPad over OpenOCD/JTAG).
- The config is sent to the injector as JSON; per-injection results stream back
  as line-delimited JSON.

WINDOW LAYOUT
- Left: the Configuration panel (set up a campaign here).
- Right: four tabs - Live monitor, Results, Report, and Assistant (this chat).
- Top: a title bar with a Hardware status badge.
- Far-left sidebar: icons. NOTE: the History and Reports sidebar icons are
  placeholders and are not wired up yet.

CONFIGURATION PANEL (fields)
- Hardware: Tiva-C (Bare Metal) or QEMU (Emulation).
- Sensor: one of WSS, TPS, MAP, ECT, APS, BPS. Selecting a sensor auto-fills its
  valid range and ASIL level from the sensor knowledge base.
- Fault type: Sensor Corruption, Memory Corruption, Bit Flip, PC Error, Task Delay.
- Variable: the name of the variable to inject into (e.g. speed_RPM), taken
  from the hardware application's documentation. The injector resolves the name
  to an address on the target.
- System state: for Sensor Corruption and Task Delay only, the variable to
  monitor to judge whether the system reached its safe state.
- Address: for PC Error only — the Variable box becomes a hex Address, since the
  program counter has no symbol name.
- Duration: how long the fault is actively held on the signal, in seconds.
- Delay: how long to wait after the run starts before the fault is injected, in ms.
- Min value / Max value: the valid operating range of the sensor signal.
- Fault value: the value written for corruption faults; for Task Delay this field
  becomes the Delay magnitude (ms).
- Bit position: which bit is flipped, for the Bit Flip fault.
- ASIL level: the target's safety integrity level (A-D); this sets the pass/fail
  thresholds for the campaign.
- Expected behavior: the safe-state response you expect when the fault occurs
  (e.g. "cruise control disengages"). If left blank, it is auto-filled from the
  sensor's safe-state knowledge base. It is the basis for deciding PASS/FAIL.

SENSORS
- WSS  Wheel Speed Sensor          0-255 km/h   ASIL-D   (vehicle speed)
- TPS  Throttle Position Sensor    0-100 %      ASIL-C   (actual throttle plate angle)
- MAP  Manifold Absolute Pressure  10-110 kPa   ASIL-B   (engine load / fuelling)
- ECT  Engine Coolant Temperature  -40 to 130 C ASIL-B   (engine management)
- APS  Accelerator Pedal Sensor    0-100 %      ASIL-D   (driver demand)
- BPS  Brake Pressure Sensor       0-200 bar    ASIL-D   (braking)

FAULT TYPES
- Sensor Corruption: forces a sensor variable to a wrong value. Has a System state
  variable to monitor for the safe-state check.
- Memory Corruption: writes an out-of-range value into a memory variable.
- Bit Flip: flips a single bit in the variable's memory.
- PC Error: corrupts the program counter to a target address (uses an Address, not
  a variable name; carries no value).
- Task Delay: delays a task by a given magnitude in ms (the Fault value field becomes
  the delay). Has a System state variable to monitor.

IMPLEMENTED SCOPE (important - be accurate about this)
The five fault types above are what is actually implemented and selectable in the
current build: Sensor Corruption and Memory Corruption (data faults), Bit Flip (a
memory fault), PC Error (a control-flow fault), and Task Delay (a timing fault).
If asked what fault types can be injected, answer with these five.

HOW TO RUN A CAMPAIGN
1. On the left Configuration panel, choose the hardware, sensor, and fault type.
2. The sensor's range and ASIL level auto-fill; enter the variable name; adjust if needed.
3. Press "Run injection".
4. The Live monitor tab shows a confirmation banner - press "Confirm" to start
   (or "Edit" to change the configuration first).
5. The log fills in real time as each fault is injected.
6. When it finishes, open the Results tab for the metrics/charts/table, and the
   Report tab for the ISO 26262 report.

LIVE MONITOR TAB
Shows the streaming log, a progress bar, and live fault-handling counts (Safe /
Unsafe) during a run.

RESULTS TAB
Shows metric cards (handling rate, total tests, average reaction time, overhead),
a fault-handling-by-type chart, a reaction-time-distribution chart, and a per-test
breakdown table (each injection: system response, reaction time, Pass/Fail outcome).

REPORT TAB
Shows an ISO 26262 report: Executive Summary, Key Metrics, an ISO 26262
Compliance table (the acceptance criteria), Key Findings, and Recommendations.
Buttons: "Export PDF" saves the report as a PDF file; "Copy" copies the text.

KEY CONCEPTS
- Test case / injection: one fault injection. The injector halts the target,
  writes the fault, resumes, and observes how the system responds.
- PASS (handled / Safe): the system reached its expected safe state within the
  FTTI - it overrode/rejected the faulty value (good). FAIL (Unsafe): the fault
  propagated, or no safe-state transition occurred, or it happened too late.
- Fault-handling rate = handled (Pass) / total, as a percentage.
- A FAIL is one of: (a) fault not detected -> propagated to output; (b) detected
  but no safe-state transition; (c) safe state reached LATE (reaction > FTTI).
- In the current mock build, the system's reaction is simulated (handles ~96%,
  a few reactions land outside the FTTI). The real
  injector observes the actual target behaviour.

ISO 26262 PASS/FAIL (the verdict)
A campaign PASSES only if ALL THREE deterministic criteria are met:
1. Fault-handling rate >= the ASIL target:  A 60%, B 70%, C 80%, D 90%.
2. Worst-case reaction time <= the ASIL FTTI:  A 50ms, B 30ms, C 20ms, D 10ms.
3. Performance overhead <= 5%.
The verdict is computed in code (ResultsManager), never by this assistant. If any
single criterion fails, the whole campaign fails.
"""


def format_results_context(data: dict) -> str:
    """Turn the latest campaign payload into a short text block for the model.
    Defensive: never raises, so it can't silently wipe the assistant's context."""
    cfg = data.get("config")

    def g(obj, key):  # read an attr whether cfg is a dataclass or a dict
        if isinstance(obj, dict):
            return obj.get(key)
        return getattr(obj, key, None)

    lines = [
        f"Sensor: {g(cfg,'sensor')} | Fault type: {g(cfg,'fault_type')} | "
        f"ASIL: {g(cfg,'asil_level')} | Hardware: {g(cfg,'hardware')}",
        f"Total tests: {data.get('total')} | Handled (safe state): {data.get('handled')} | "
        f"Handling rate: {data.get('handling_pct')}%",
        f"Avg reaction: {data.get('avg_reaction_ms')}ms | Max reaction: "
        f"{data.get('max_reaction_ms')}ms (FTTI {data.get('ftti_ms')}ms) | "
        f"Overhead: {data.get('overhead_pct')}%",
        f"Verdict (passed_asil): {data.get('passed_asil')}",
    ]
    if data.get("criteria"):
        lines.append("Acceptance criteria:")
        for c in data["criteria"]:
            try:
                name, value, target, ok = c
                lines.append(f"  - {name}: {value} (target {target}) -> "
                             f"{'PASS' if ok else 'FAIL'}")
            except Exception:
                continue
    return "\n".join(lines)


def _build_system_prompt(results_context: str) -> str:
    prompt = (
        "You are the built-in assistant for the Fault Injection Framework, a desktop "
        "tool for ISO 26262 automotive safety testing. Be genuinely helpful, "
        "knowledgeable, and substantive.\n\n"
        "HOW TO ANSWER:\n"
        "- Use your full knowledge of automotive systems, sensors, embedded "
        "targets, and ISO 26262 to explain concepts clearly and accurately. If "
        "asked what a sensor, fault type, or term means, give a real, useful "
        "explanation - never a thin answer like 'it's a field in the panel'.\n"
        "- For questions specific to THIS framework (which sensors or fault types "
        "it supports, the exact pass/fail thresholds, the window layout, or how to "
        "do something in the app), treat the KNOWLEDGE BASE and PROJECT REFERENCE "
        "below as the authority and prefer them over general knowledge.\n"
        "- Good pattern for 'what does X mean' questions: briefly explain the "
        "general concept first, then add what it specifically is in this framework. "
        "(e.g. for ASIL: a sentence or two on what ASIL is in ISO 26262, then this "
        "framework's specific target table.)\n"
        "- For 'how do I...' questions, give clear numbered steps from the "
        "knowledge base.\n"
        "- When the user refers to 'the campaign I ran', 'the last run', 'my "
        "results', or 'the fault I just injected', they mean the LATEST CAMPAIGN "
        "RESULTS section below - use it directly to answer or to write a report. Do "
        "NOT ask them which campaign or to re-enter parameters; the results are "
        "already provided to you.\n\n"
        "LIMITS:\n"
        "- Do NOT invent framework features, menus, buttons, tabs, or steps that "
        "aren't in the knowledge base or reference. If unsure whether the framework "
        "has a particular feature, say so rather than guessing.\n"
        "- If the KNOWLEDGE BASE and the PROJECT REFERENCE ever disagree about what "
        "the software actually does or includes, the KNOWLEDGE BASE wins - it "
        "reflects the current build, while the thesis may describe designed or "
        "planned features that aren't implemented yet.\n"
        "- Never recompute or change a pass/fail verdict; report it as given.\n"
        "- ABOUT YOURSELF: you are the framework's assistant, powered by an AI "
        "language model accessed through an online service - so you DO need an "
        "internet connection. Never claim to run offline, locally, or without "
        "internet; that is inaccurate. If asked how you work, answer briefly and "
        "accurately, don't discuss API keys or internal details, and steer back to "
        "helping with the framework.\n"
        "- Be concise but substantive - depth where it helps, no padding.\n\n"
        "=== KNOWLEDGE BASE ===\n" + FRAMEWORK_KB
    )
    if REFERENCE:
        prompt += "\n\n=== PROJECT REFERENCE (thesis excerpts) ===\n" + REFERENCE
    if results_context:
        prompt += "\n\n=== LATEST CAMPAIGN RESULTS ===\n" + results_context
    else:
        prompt += ("\n\n=== LATEST CAMPAIGN RESULTS ===\n(No campaign has finished "
                   "yet this session. If the user asks about results or wants a "
                   "report, tell them to run a campaign first - do NOT ask them to "
                   "type campaign parameters manually.)")
    return prompt


class ChatWorker(QThread):
    """Sends the conversation to the Gemini model and returns the reply."""
    reply_ready = pyqtSignal(str)
    error       = pyqtSignal(str)

    def __init__(self, history, results_context: str, parent=None):
        super().__init__(parent)
        self.history = history                  # [{"role": ..., "content": ...}, ...]
        self.results_context = results_context

    def run(self):
        try:
            from openai import OpenAI
        except ImportError:
            self.error.emit("The 'openai' package isn't installed. Run: pip install openai")
            return

        api_key = os.environ.get("GEMINI_API_KEY")
        if not api_key:
            self.error.emit(
                "GEMINI_API_KEY isn't set. Set it in the same terminal you launch "
                "the app from (PowerShell: $env:GEMINI_API_KEY=\"your-key\"), then "
                "relaunch. If you run from VS Code, set it in that terminal or in "
                "launch.json."
            )
            return

        try:
            client = OpenAI(api_key=api_key, base_url=BASE_URL)
            messages = [{"role": "system", "content": _build_system_prompt(self.results_context)}]
            messages += self.history[-10:]      # keep the last few turns for context
            resp = client.chat.completions.create(
                model=MODEL,
                messages=messages,
                max_tokens=700,
                temperature=0.3,                # low = stick to the facts
            )
            text = (resp.choices[0].message.content or "").strip()
            self.reply_ready.emit(text or "(the model returned an empty response)")
        except Exception as e:
            self.error.emit(
                f"Couldn't get a response from Gemini (model '{MODEL}'). Check your "
                f"GEMINI_API_KEY and your internet connection.\n\n({e})"
            )