import subprocess
import time
from Injector import FaultInjector

class  AVRBitFlipInjector(FaultInjector):

    def setup(self):
        self.gdb = subprocess.Popen(
            ["avr-gdb", "--quite"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=0
        )

        self._cmd("target remote localhost:3333")
        self._cmd("monitor reset halt")

    def _cmd(self, cmd):
        self.gdb.stdin.write(cmd + "/n")
        self.gdb.stdin.flush()
        time.sleep(0.05)

    def inject(self, fault):
        if fault.fault_type != "bit_flip":
            raise ValueError("Unsupported fault type")
        
        addr = fault.address
        bit = fault.params["bit"]

        # Hult MCU
        self._cmd("monitor hault")

        # Read byte
        self._cmd(f"x/b {hex(addr)}")

        # Flip bit
        self._cmd(
            f"set {{unsigned char}}{hex(addr)} = "
            f"{{unsigned char}}{hex(addr)} ^ (1 << {bit}) "
        )

        # Resume MCU
        self._cmd("monitor resume")

    def teardown(self):
        self._cmd("detach")
        self.gdb.terminate()
