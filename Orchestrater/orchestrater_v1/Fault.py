from dataclasses import dataclass


@dataclass
class Fault:
    def __init__(self, fault_type, address, params, time=None):
        fault_type: str             # bit_flip
        address: int                # SRAM/register address
        time: float                 # seconds since start
        params: dict                # example {"bit" : 3}
    