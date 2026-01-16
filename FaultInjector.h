#ifndef FAULT_INJECTOR_H
#define FAULT_INJECTOR_H

#include <string>
#include <functional>
#include <vector>
#include <chrono>
#include <memory>
#include "Session.h"

enum class FaultType {
    REGISTER_BIT_FLIP,
    MEMORY_CORRUPTION,
    PC_MODIFICATION
};

enum class ExecutionResult {
    SUCCESS,
    CRASH,
    TIMEOUT,
    WRONG_OUTPUT,
    DETECTED,
    UNKNOWN
};

struct FaultConfig {
    FaultType type;
    
    // For register bit flip
    std::string registerName;
    int bitPosition;
    
    // For memory corruption
    uint16_t memoryAddress;
    uint8_t memoryValue;
    
    // For PC modification
    uint16_t newPC;
    
    // Timing
    uint32_t injectionTimeMs;  // When to inject the fault after start
};

struct FaultResult {
    FaultConfig config;
    ExecutionResult result;
    std::string errorMessage;
    uint64_t executionTimeMs;
    std::vector<uint8_t> outputData;
    bool faultDetected;
};

class FaultInjector {
private:
    Session* session;
    pid_t sessionPid;
    
    // Callbacks for checking execution
    std::function<bool(const std::vector<uint8_t>&)> outputValidator;
    std::function<bool()> crashDetector;
    
    uint32_t timeoutMs;
    
    // Internal methods
    ExecutionResult monitorExecution();
    bool injectFault(const FaultConfig& config);
    std::vector<uint8_t> captureOutput();
    bool waitForInjectionTime(uint32_t timeMs);
    
public:
    FaultInjector(Session* sess, uint32_t timeout = 5000);
    ~FaultInjector();
    
    // Configure validators
    void setOutputValidator(std::function<bool(const std::vector<uint8_t>&)> validator);
    void setCrashDetector(std::function<bool()> detector);
    
    // Run single fault injection
    FaultResult runFaultInjection(const FaultConfig& config);
    
    // Run multiple fault injections (fault campaign)
    std::vector<FaultResult> runFaultCampaign(const std::vector<FaultConfig>& configs);
    
    // Generate fault configurations for comprehensive testing
    static std::vector<FaultConfig> generateRegisterBitFlipCampaign(
        const std::vector<std::string>& registers,
        uint32_t injectionTime);
    
    static std::vector<FaultConfig> generateMemoryCampaign(
        uint16_t startAddr, 
        uint16_t endAddr,
        uint32_t injectionTime);
};


#endif
