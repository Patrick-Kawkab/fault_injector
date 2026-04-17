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
    virtual ~Session()= default;
    virtual int start()=0;
    virtual int stop()=0;
    virtual int setPC(uint16_t pc)=0;
    virtual bool memoryCorruptionTest(uint32_t addr,uint8_t injectedValue,uint8_t minExpected,  uint8_t maxExpected)=0;
    bool bitFlipTest(uint32_t addr,   uint8_t  bitPos,
                     uint8_t  minExp, uint8_t  maxExp,
                     uint64_t targetCount);    static std::unique_ptr<Session>create(const std::string& type);
    bool instructionSkipTest(uint32_t addr);
    bool sensorCorruptionTest(uint32_t sensorAddr,
                              uint8_t  spoofedValue,
                              uint8_t  minExpected,
                              uint8_t  maxExpected);
};
#endif