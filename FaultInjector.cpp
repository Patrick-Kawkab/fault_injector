// FaultInjector.cpp
#include "FaultInjector.h"
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <thread>
#include <cstring>

FaultInjector::FaultInjector(Session* sess, uint32_t timeout)
    : session(sess), sessionPid(-1), timeoutMs(timeout)
{
    if (!session) {
        throw std::invalid_argument("Session pointer cannot be null");
    }
}

FaultInjector::~FaultInjector()
{
    if (sessionPid > 0) {
        kill(sessionPid, SIGKILL);
        waitpid(sessionPid, nullptr, 0);
    }
}

void FaultInjector::setOutputValidator(std::function<bool(const std::vector<uint8_t>&)> validator)
{
    outputValidator = validator;
}

void FaultInjector::setCrashDetector(std::function<bool()> detector)
{
    crashDetector = detector;
}

bool FaultInjector::waitForInjectionTime(uint32_t timeMs)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(timeMs));
    return true;
}

bool FaultInjector::injectFault(const FaultConfig& config)
{v
    std::cout << "Injecting fault of type: " << static_cast<int>(config.type) << std::endl;
    
    int result = -1;
    
    switch (config.type) {
        case FaultType::REGISTER_BIT_FLIP:
            std::cout << "Flipping bit " << config.bitPosition 
                      << " in register " << config.registerName << std::endl;
            result = session->flipRegisterBit(config.registerName, config.bitPosition);
            break;
            
        case FaultType::MEMORY_CORRUPTION:
            std::cout << "Writing value 0x" << std::hex << (int)config.memoryValue 
                      << " to address 0x" << config.memoryAddress << std::dec << std::endl;
            result = session->writeMemory(config.memoryAddress, config.memoryValue);
            break;
            
        case FaultType::PC_MODIFICATION:
            std::cout << "Setting PC to 0x" << std::hex << config.newPC << std::dec << std::endl;
            result = session->setPC(config.newPC);
            break;
    }
    
    return result == 0;
}

std::vector<uint8_t> FaultInjector::captureOutput()
{
    // This is a placeholder - actual implementation depends on how output is captured
    // Could read from serial port, memory location, file, etc.
    std::vector<uint8_t> output;
    
    // Example: read from a known memory location or file
    // This would be customized based on the target system
    
    return output;
}

ExecutionResult FaultInjector::monitorExecution()
{
    auto startTime = std::chrono::steady_clock::now();
    
    while (true) {
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - startTime).count();
        
        // Check for timeout
        if (elapsed > timeoutMs) {
            std::cout << "Execution timeout after " << elapsed << "ms" << std::endl;
            return ExecutionResult::TIMEOUT;
        }
        
        // Check if process crashed
        if (sessionPid > 0) {
            int status;
            pid_t result = waitpid(sessionPid, &status, WNOHANG);
            
            if (result > 0) {
                if (WIFSIGNALED(status)) {
                    std::cout << "Process crashed with signal: " 
                              << WTERMSIG(status) << std::endl;
                    return ExecutionResult::CRASH;
                }
                else if (WIFEXITED(status)) {
                    int exitCode = WEXITSTATUS(status);
                    std::cout << "Process exited with code: " << exitCode << std::endl;
                    
                    if (exitCode == 0) {
                        return ExecutionResult::SUCCESS;
                    } else {
                        return ExecutionResult::WRONG_OUTPUT;
                    }
                }
            }
        }
        
        // Use custom crash detector if provided
        if (crashDetector && crashDetector()) {
            std::cout << "Crash detected by custom detector" << std::endl;
            return ExecutionResult::CRASH;
        }
        
        // Small delay to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return ExecutionResult::UNKNOWN;
}

FaultResult FaultInjector::runFaultInjection(const FaultConfig& config)
{
    FaultResult result;
    result.config = config;
    result.faultDetected = false;
    result.result = ExecutionResult::UNKNOWN;
    
    auto startTime = std::chrono::steady_clock::now();
    
    std::cout << "\n=== Starting Fault Injection Experiment ===" << std::endl;
    
    // Fork the process
    sessionPid = fork();
    
    if (sessionPid == 0) {
        // Child process - run the session
        std::cout << "[Child] Starting session..." << std::endl;
        
        int startResult = session->start();
        if (startResult != 0) {
            std::cerr << "[Child] Failed to start session" << std::endl;
            _exit(1);
        }
        
        // Child continues running - parent will inject fault
        pause();  // Wait for signal
        _exit(0);
    }
    else if (sessionPid > 0) {
        // Parent process - inject fault and monitor
        std::cout << "[Parent] Session forked with PID: " << sessionPid << std::endl;
        
        // Wait for the specified injection time
        if (config.injectionTimeMs > 0) {
            std::cout << "[Parent] Waiting " << config.injectionTimeMs 
                      << "ms before fault injection..." << std::endl;
            waitForInjectionTime(config.injectionTimeMs);
        }
        
        // Inject the fault
        bool injectionSuccess = injectFault(config);
        if (!injectionSuccess) {
            result.errorMessage = "Failed to inject fault";
            result.result = ExecutionResult::UNKNOWN;
            
            // Clean up
            kill(sessionPid, SIGTERM);
            waitpid(sessionPid, nullptr, 0);
            sessionPid = -1;
            
            return result;
        }
        
        std::cout << "[Parent] Fault injected successfully, monitoring execution..." << std::endl;
        
        // Monitor the execution
        result.result = monitorExecution();
        
        // Capture output
        result.outputData = captureOutput();
        
        // Validate output if validator is provided
        if (outputValidator && !result.outputData.empty()) {
            bool outputValid = outputValidator(result.outputData);
            if (!outputValid && result.result == ExecutionResult::SUCCESS) {
                result.result = ExecutionResult::WRONG_OUTPUT;
            }
        }
        
        // Check if fault was detected by the system
        if (result.result == ExecutionResult::DETECTED || 
            result.result == ExecutionResult::SUCCESS) {
            result.faultDetected = (result.result == ExecutionResult::DETECTED);
        }
        
        // Clean up
        if (sessionPid > 0) {
            kill(sessionPid, SIGTERM);
            waitpid(sessionPid, nullptr, 0);
            sessionPid = -1;
        }
        
        session->stop();
    }
    else {
        // Fork failed
        std::cerr << "Failed to fork process" << std::endl;
        result.errorMessage = "Fork failed: " + std::string(strerror(errno));
        result.result = ExecutionResult::UNKNOWN;
        return result;
    }
    
    auto endTime = std::chrono::steady_clock::now();
    result.executionTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();
    
    std::cout << "=== Fault Injection Complete ===" << std::endl;
    std::cout << "Result: " << static_cast<int>(result.result) << std::endl;
    std::cout << "Execution time: " << result.executionTimeMs << "ms" << std::endl;
    
    return result;
}

std::vector<FaultResult> FaultInjector::runFaultCampaign(
    const std::vector<FaultConfig>& configs)
{
    std::vector<FaultResult> results;
    results.reserve(configs.size());
    
    std::cout << "\n===== Starting Fault Campaign =====" << std::endl;
    std::cout << "Total experiments: " << configs.size() << std::endl;
    
    for (size_t i = 0; i < configs.size(); ++i) {
        std::cout << "\n--- Experiment " << (i + 1) << "/" << configs.size() 
                  << " ---" << std::endl;
        
        FaultResult result = runFaultInjection(configs[i]);
        results.push_back(result);
        
        // Small delay between experiments
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // Print summary
    std::cout << "\n===== Campaign Summary =====" << std::endl;
    int crashes = 0, timeouts = 0, wrongOutputs = 0, detected = 0, success = 0;
    
    for (const auto& r : results) {
        switch (r.result) {
            case ExecutionResult::CRASH: crashes++; break;
            case ExecutionResult::TIMEOUT: timeouts++; break;
            case ExecutionResult::WRONG_OUTPUT: wrongOutputs++; break;
            case ExecutionResult::DETECTED: detected++; break;
            case ExecutionResult::SUCCESS: success++; break;
            default: break;
        }
    }
    
    std::cout << "Total: " << results.size() << std::endl;
    std::cout << "Crashes: " << crashes << std::endl;
    std::cout << "Timeouts: " << timeouts << std::endl;
    std::cout << "Wrong outputs: " << wrongOutputs << std::endl;
    std::cout << "Faults detected: " << detected << std::endl;
    std::cout << "Successful: " << success << std::endl;
    
    return results;
}

std::vector<FaultConfig> FaultInjector::generateRegisterBitFlipCampaign(
    const std::vector<std::string>& registers,
    uint32_t injectionTime)
{
    std::vector<FaultConfig> configs;
    
    // For each register, flip each bit (assuming 8-bit registers)
    for (const auto& reg : registers) {
        for (int bit = 0; bit < 8; ++bit) {
            FaultConfig config;
            config.type = FaultType::REGISTER_BIT_FLIP;
            config.registerName = reg;
            config.bitPosition = bit;
            config.injectionTimeMs = injectionTime;
            
            configs.push_back(config);
        }
    }
    
    return configs;
}

std::vector<FaultConfig> FaultInjector::generateMemoryCampaign(
    uint16_t startAddr, 
    uint16_t endAddr,
    uint32_t injectionTime)
{
    std::vector<FaultConfig> configs;
    
    // Generate bit flips for each byte in the memory range
    for (uint16_t addr = startAddr; addr <= endAddr; ++addr) {
        // Flip each bit in the byte
        for (int bit = 0; bit < 8; ++bit) {
            FaultConfig config;
            config.type = FaultType::MEMORY_CORRUPTION;
            config.memoryAddress = addr;
            config.memoryValue = (1 << bit);  // Single bit flip
            config.injectionTimeMs = injectionTime;
            
            configs.push_back(config);
        }
    }
    
    return configs;
}