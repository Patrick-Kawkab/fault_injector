import json
from Fault import Fault

def load_experiment(input):
    with open(input, "r") as f:
        data = json.load(f)

    faults = []
    for fdef in data["faults"]:
        fault = Fault(
            fault_type = fdef["fault_type"],
            address = int(fdef["address"], 16),
            time = fdef["time"],
            params = fdef["params"]
        )
        faults.append(fault)

    return faults, data.get("campain_name", "unnamed")
