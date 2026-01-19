#include"Session.h"

class HardwareSession:public Session{
    private:

    public:
    HardwareSession(){
        
    }
    int start() override;
    int stop() noexcept override;
    int setPC(uint16_t pc) override;
    int checkPC() override;
    int flipRegisterBit(std::string reg, int bit)override;
    int writeMemory(uint16_t addr, uint8_t value)override;
};