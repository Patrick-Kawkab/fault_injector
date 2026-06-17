#ifndef HARDWARE_SESSION_H
#define HARDWARE_SESSION_H

#include "Session.h"
#include <string>

class HardwareSession : public Session {
public:
    HardwareSession(const std::string& host = "localhost", int port = 4444);
    ~HardwareSession() override;

    int start() override;
    int stop() noexcept override;

    // ── Session interface ────────────────────────────────────────────
    // FaultConfig.h is the QEMU<->plugin wire format and has no
    // dedicated "duration/delay" or "interval" fields, so the following
    // FaultDescriptor fields are repurposed for hardware timing/state:
    //   target_count -> duration / delay / wait, in milliseconds
    //   sensor_addr  -> address to verify after injection (cruise_state)
    //   target_addr  -> (Task_delay only) corrupted value to write
    FaultResult setPC(const FaultDescriptor& desc) override;
    FaultResult memoryCorruptionTest(const FaultDescriptor& desc) override;
    FaultResult bitFlipTest(const FaultDescriptor& desc) override;
    FaultResult Task_delay(const FaultDescriptor& desc) override;
    FaultResult sensorCorruptionTest(const FaultDescriptor& desc) override;

private:
    int sockfd;
    std::string host;
    int port;

    bool sendCmd(const std::string& cmd);
    std::string recvResponse();
};

#endif