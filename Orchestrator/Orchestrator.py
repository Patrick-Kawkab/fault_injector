import logging
import subprocess


#logger = logging.getLogger(__name__)
#logging.basicConfig(filename="orchest.log",
#                   level=logging.INFO,
#                  format='%(asctime)s - %(levelname)s - %(message)s',
#                 filemode= "w"
#                )

class Orchestrator:
    def __init__(self):
        subprocess.run(["./TUI.sh"], check=True)
        
if __name__ == "__main__":
    Orchestrator()
