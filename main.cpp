#define CONFIG_JSON_PATH   "Input.json"
#define ELF_FILE_PATH     "firmware/tiva_led.elf"

#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdint>

#include "HardSession.h"
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
    const int faultID              = config["id"];
    const std::string firmware     = config["Firmware"];
    const std::string mode         = config["Mode"];
    const std::string target       = config["Target"];
    const std::string fault_type   = config["fault_type"];
    const uint8_t max              = config["max"];
    const uint8_t min              = config["min"];
    const uint32_t address         = config["address"]
    const uint8_t value            = config["value"]
    if(mode=="HARDWARE" && fault_type=="memory_corruption"){
        HardwareSession hardware;
        hardware.start();
        
        std::cout << "[INFO] " << targetSymbol
        /* ---------------- Resolve symbol ---------------- */
        volatile uint32_t systemStateAddr;
        try {
            systemStateAddr = getSymbolAddressFromELF(targetSymbol);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] " << e.what() << "\n";
            return -1;
        }
        << " @ 0x" << std::hex << systemStateAddr << std::dec << "\n";
        hardware.memoryCorruptionTest(systemStateAddr,value,minmax);
    }
}