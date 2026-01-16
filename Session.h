#ifndef SESSION_H
#define SESSION_H
#include <iostream>
#include <string>
#include <unistd.h>
#include <signal.h>
#include <fstream>
#include <sys/wait.h>
#include "QemuSession.h"

class Session {
public:
    virtual ~Session() = default;
    virtual int setPC(uint16_t pc) = 0;
    virtual int checkPC() = 0;
    virtual int flipRegisterBit(std::string reg, int bit) = 0;
    virtual int writeMemory(uint16_t addr, uint8_t value) = 0;
    virtual int start() = 0;
    virtual int stop() noexcept = 0;
};
#endif