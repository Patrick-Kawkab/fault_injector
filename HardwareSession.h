#ifndef HARDWARE_SESSION_H
#define HARDWARE_SESSION_H

#include "Session.h"
#include <string>

class HardwareSession : public Session {
public:
    HardwareSession(const std::string& host = "localhost", int port = 4444);
    ~HardwareSession();

    int start() override;
    int stop() noexcept override;

    int setPC(uint16_t pc) override;

    bool memoryCorruptionTest(uint32_t addr,uint8_t injectedValue,uint8_t minExpected,  uint8_t maxExpected) override;

private:
    int sockfd;
    std::string host;
    int port;

    bool sendCmd(const std::string& cmd);
    std::string recvResponse();
};

#endif
