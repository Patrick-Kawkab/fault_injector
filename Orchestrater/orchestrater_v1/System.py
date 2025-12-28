import subprocess
import time

class AvrSystemController:

    def reset(self):
        subprocess.run(
            ["openocd", "-c", "reset halt"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        time.sleep(0.2)

    def run(self):
        subprocess.run(
            ["openocd", "-c", "resume"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
