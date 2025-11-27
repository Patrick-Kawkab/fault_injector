#include "faultInjector.h"
#include <cstdint>
#include "iostream"

class BitFlipInjector : public FaultInjector{
private:
    int Register ,bit;
public:
    BitFlipInjector(int reg ,int b) 
        : FaultInjector("memory corruption") ,Register(reg),bit(b){};

    ~BitFlipInjector(){};

    void inject() override{
        std::cout << "Flipping bit " << bit
                  << " in register " << Register << std::endl;
        qemu.flipRegisterBit(Register ,bit);
    }
};