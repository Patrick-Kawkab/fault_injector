#define CONFIG_JSON_PATH   "./Orchestrator/Input.json"
#define ELF_FILE_DIR       "./tiva_c/"

#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdint>

#include "HardwareSession.h"
#include "json.hpp"

using json = nlohmann::json;

uint32_t getSystemStateAddress(const std::string& elfPath ,const std::string& address)
{
        std::stringstream cmd;
    cmd << "arm-none-eabi-nm " << elfPath
        << " | awk '$3==\"" << address << "\" {print $1}'";

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

    const auto& fault = config["faults"][0];
    const int faultID              = fault["id"];
    const std::string firmware     = fault["Firmware"];
    const std::string mode         = fault["Mode"];
    const std::string target       = fault["Target"];
    const std::string fault_type   = fault["fault_type"];
    const uint8_t max              = fault["max"];
    const uint8_t min              = fault["min"];
    const std::string address      = fault["address"];
    const uint8_t value            = fault["value"];       
    /* Construct full ELF path */
    std::string elfPath = std::string(ELF_FILE_DIR) + firmware;

    if(mode=="HARDWARE" && fault_type=="memory_corruption"){
        HardwareSession hardware;
        hardware.start();
        /* ---------------- Resolve symbol ---------------- */
        volatile uint32_t systemStateAddr;
        try {
        systemStateAddr = getSystemStateAddress(elfPath , address);        
        }
        catch (const std::exception& e) {
            std::cerr << "[ERROR] " << e.what() << "\n";
            return -1;
        }
        std::cout << "[INFO] "
        << " 0x" << std::hex << systemStateAddr << std::dec << "\n";
        bool testResult =
            hardware.memoryCorruptionTest(systemStateAddr, value, min, max);
        json output;
        output["faults"] = json::array();

        json faultResult;
        faultResult["Firmware"]   = firmware;
        faultResult["Mode"]       = mode;
        faultResult["Target"]     = target;
        faultResult["id"]         = faultID;
        faultResult["fault_type"] = fault_type;
        faultResult["min"]        = min;
        faultResult["max"]        = max;
        faultResult["address"]    = nlohmann::json::string_t([&]
                                                            {
                                                                std::stringstream ss;
                                                                ss<<"0x"<<std::hex<<std::uppercase<<systemStateAddr;
                                                                return ss.str();
                                                            }());
        faultResult["value"]      = value;
        faultResult["result"]     = testResult ? "PASSED" : "FAILED";
        std::cout<<"fault:\n"<<"    Mode: "<<faultResult["Mode"]<<"\n"<<"    Target: "<<faultResult["Target"] <<"\n"<<"    ID: "<<faultResult["id"] <<"\n"
                        <<"    fault_type: "<<faultResult["fault_type"]<<"\n"<<"    minimum_value: "<<faultResult["min"]<<"\n"<<"    maximum_value:"<<fault["max"]<<"\n"
                        <<"    address: "<<   faultResult["address"]<<"\n"<<"    coruppted_value: "<<faultResult["value"]<<"\n"<<"    test_result: "<<faultResult["result"] <<std::endl;
        output["faults"].push_back(faultResult);

        /* Write output JSON */
        std::ofstream out("campaign_result.json");
        out << output.dump(4);
        out.close();

        std::cout << "[INFO] Campaign result written to campaign_result.json"<<std::endl;
    }
}