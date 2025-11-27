#include "faultInjector.h"
#include <cstdint>
#include "iostream"

class MemoryCorruptionInjector : public FaultInjector {
    private:
        uint16_t address;
        uint8_t value;

    public:
        MemoryCorruptionInjector(uint16_t addr, uint8_t val) 
            : FaultInjector("memory corruption") , address(addr) ,value(val) {};
    
        ~MemoryCorruptionInjector(){};
    
        void inject() override {
        std::cout << "Corrupting memory at 0x"
                  << std::hex << address
                  << " with value 0x" << std::hex << (int)value
                  << std::dec << std::endl;

        qemu.writeMemory(address ,value)
    };
};