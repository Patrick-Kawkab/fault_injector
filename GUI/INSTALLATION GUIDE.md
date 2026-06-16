# Fault Injection Framework — Quick Setup

**Requirement:** Python 3.10 or newer installed (during install on Windows, tick
*"Add python.exe to PATH"*).

Open **PowerShell inside the project folder** (the one containing `main.py`) and
run these three commands:

```powershell
# 1. Install the dependencies (one time only)
pip install PyQt5 openai

# 2. Set your Gemini API key — free key at https://aistudio.google.com/apikey
#    (needed only for the Assistant chat tab; everything else runs without it)
$env:GEMINI_API_KEY="paste-your-key-here"

# 3. Run the app
python main.py
```

That's it — the app opens and you can run a test campaign right away (it uses a
built-in mock injector, so no hardware is needed).

> Note: the key from step 2 lasts only for that PowerShell window. To avoid
> re-typing it each time, set it permanently via Windows *"Edit environment
> variables for your account"* → New → name `GEMINI_API_KEY`.
