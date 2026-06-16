#ifndef HARDWARE_SESSION_H
#define HARDWARE_SESSION_H

#include "Session.h"
#include <string>

class HardwareSession {
public:
    HardwareSession(const std::string& host = "localhost", int port = 4444);
    ~HardwareSession();

    int start() ;
    int stop() noexcept ;
    bool sensorCorruptionTest(
        uint32_t encoderCountAddr,
        uint32_t cruiseStateAddr,
        uint32_t durationMs,
        uint32_t intervalMs = 50
    );
    bool taskDelayTest(
        uint32_t samplePeriodAddr,
        uint32_t cruiseStateAddr,
        uint32_t corruptedValue,
        uint32_t duration_ms,
        uint32_t interval_ms
    );
    bool pcCorruptionTest(uint32_t badPC, uint32_t cruiseStateAddr, uint32_t wait_ms);
    bool task_delay(uint32_t addr,uint8_t delay_ms, uint32_t cruiseStateAddr);
    bool bitFlip(uint32_t addr, uint8_t bit_position, uint8_t minExpected, uint8_t maxExpected, uint8_t delay_ms);

    bool memoryCorruptionTest(uint32_t addr,uint8_t injectedValue,uint8_t minExpected,  uint8_t maxExpected,uint8_t delay_ms) ;

private:
    int sockfd;
    std::string host;
    int port;

    bool sendCmd(const std::string& cmd);
    std::string recvResponse();
};

#endif
