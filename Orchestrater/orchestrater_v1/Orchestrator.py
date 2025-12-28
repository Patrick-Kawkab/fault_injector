import time
import json
import logging

from AVR_Injector import AvrBitFlipInjector
from System import AvrSystemController
from Monitor import Monitor
from Orchestrator.Experiment_loader import load_experiment

logger = logging.getLogger(__name__)
logging.basicConfig(filename="orchestrator.log", 
                    level=logging.INFO,
                    format='%(asctime)s - %(levelname)s - %(message)s',
                    filemode= "w"
                )

class Orchestrator:

    # Initializing injector
    def __init__(self):
        self.injector = AvrBitFlipInjector()
        self.system   = AvrSystemController()
        self.monitor  = Monitor()

    # Running Injector
    def run(self):

        print("[*] Setting up injector")

        self.injector.setup()
        logging.info("Injector setup!")

        faults, campaign = load_experiment("Input.json") 
        logging.info("JSON file uploaded")

        results = []

        for i, fault in enumerate(faults):
            print(f"[+] Campaign {campaign} ,run {i}")

            # Resetting monitor to clear everything
            self.system.reset()
            logging.info("Monitor reset!")
            
            # Starting Monitor back up
            self.monitor.start()
            logging.info("Monitor start!")

            start_time = time.time()
            self.system.run()
            logging.info("System running!")

            # Wait until injection time
            if fault.time is not None:

                while time.time() - start_time < fault.time:
                    time.sleep(0.001)

                print(f"[!] Injecting fault at t={fault.time}s")
            else:
                print(f"[!] Injecting fault immedaitely")
                
            self.injector.inject(fault)

            # Let system run after injection
            time.sleep(0.5)

            result = self.monitor.collect()

            run_data = {
                "campain": campaign,
                "run": i,
                "fault": {
                    "fault type": fault.fault_type,
                    "address": hex(fault.address),
                    "bit": fault.bit,
                    "time": fault.time
                }
            }

            results.append(result)

            print("[*] Result:", result)
        
        with open("results_json" ,"w") as f:
            json.dump(results, f, indent=2)
        print("Results written to JSON file")

        print("[*] Tearing down injector")
        self.injector.teardown()


if __name__ == "__main__":
    Orchestrator().run()