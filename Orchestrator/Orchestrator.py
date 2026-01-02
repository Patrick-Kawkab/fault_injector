import time
import json
import logging
import subprocess
import os

logger = logging.getLogger(__name__)
logging.basicConfig(filename="orchest.log",
                    level=logging.INFO,
                    format='%(asctime)s - %(levelname)s - %(message)s',
                    filemode= "w"
                    )

class Orchestrator:
    def __init__(self):
        subprocess.run(["./TUI.sh"], check=True)

        RESULT_FILE = os.path.expanduser("~/Desktop/Result.json")

        while not os.path.exists(RESULT_FILE):
            time.sleep(0.5)

        with open(RESULT_FILE, "r") as f:
            result = json.load(f)

        print(result)

if __name__ == "__main__":
    Orchestrator()