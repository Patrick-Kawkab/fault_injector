#ifndef SESSION_H
#define SESSION_H
#include <iostream>
#include <string>
#include <unistd.h>
#include <signal.h>
#include <fstream>
#include <sys/wait.h>

class Session {
public:
    virtual ~Session() = default;
    virtual int setPC(uint16_t pc) = 0;
    virtual bool memoryCorruptionTest(uint32_t addr,uint8_t injectedValue,uint8_t minExpected,  uint8_t maxExpected) = 0;
    virtual int start() = 0;
    virtual int stop() noexcept = 0;
};
#endif