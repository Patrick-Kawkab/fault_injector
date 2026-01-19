#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdint>

#include "HardSession.h"
#include "GDBClient.h"
#include "json.hpp"

using json = nlohmann::json;

uint32_t getSystemStateAddress(const std::string& elfPath)
{
    std::stringstream cmd;
    cmd << "arm-none-eabi-nm " << elfPath
        << " | awk '$3==\"system_state\" {print $1}'";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run nm");
    }

    char buffer[64] = {0};
    if (!fgets(buffer, sizeof(buffer), pipe)) {
        pclose(pipe);
        throw std::runtime_error("system_state not found");
    }
    pclose(pipe);

    // Convert hex string → integer
    return static_cast<uint32_t>(std::stoul(buffer, nullptr, 16));
}


int main(){
    std::cout << "[INFO] Fault Injector started\n";

    /* ---------------- Load JSON ---------------- */
    json config;
    try {
        std::ifstream cfg(CONFIG_JSON_PATH);
        cfg >> config;
    } catch (...) {
        std::cerr << "[ERROR] Failed to read config JSON\n";
        return -1;
    }

    const std::string targetSymbol = config["target_symbol"];
    const uint32_t triggerValue    = config["trigger_value"];
    const int maxInjections        = config["max_injections"];
    const int injectDelayMs        = config["injection_delay_ms"];

    /* ---------------- Resolve symbol ---------------- */
    uint32_t systemStateAddr;
    try {
        systemStateAddr = getSymbolAddressFromELF(targetSymbol);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return -1;
    }

    std::cout << "[INFO] " << targetSymbol
              << " @ 0x" << std::hex << systemStateAddr << std::dec << "\n";
}