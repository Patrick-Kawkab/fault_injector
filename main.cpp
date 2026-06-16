#define CONFIG_JSON_PATH   "./conf.json"
#define ELF_FILE_DIR       "./Cruise_Control/Corrected"

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
        if (!cfg.is_open()) {
            std::cerr << "[ERROR] Cannot open config file at: " << CONFIG_JSON_PATH << "\n";
            return -1;
        }
        cfg >> config;
        std::cout << "[DEBUG] JSON loaded successfully\n";
        std::cout << "[DEBUG] meta type: " << config["meta"].type_name() << "\n";
        std::cout << "[DEBUG] faults type: " << config["faults"].type_name() << "\n";
        std::cout << "[DEBUG] full config: " << config.dump(2) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to read config JSON: " << e.what() << "\n";
        return -1;
    }

    const auto& meta = config["meta"];
    const auto& fault = config["faults"][0];
    const int faultID              = fault["id"];
    const std::string firmware     = meta["firmware"];
    const std::string mode         = meta["mode"];
    const std::string micro        = meta["micro"];
    const std::string server       = meta["gdb"];
    const std::string target       = meta["target"];
    const std::string asil         = meta["asil_level"];
    const std::string fault_type   = fault["fault_type"];
    const uint8_t max              = fault["max"];
    const uint8_t min              = fault["min"];
    const std::string address      = fault["address"];
    const uint8_t bit_position     = fault["bit_position"];
    const uint16_t delay_ms        = fault["delay_ms"];
    const uint16_t duration_ms     = fault["duration_ms"];
    const uint16_t interval_ms     = fault["interval_ms"];
    const std::string system_state = fault["system_state"];
    const uint8_t value            = fault["value"];       
    /* Construct full ELF path */
    std::string elfPath = std::string(ELF_FILE_DIR) + "/" + firmware;

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
            hardware.memoryCorruptionTest(systemStateAddr, value, min, max,delay_ms);
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
    if (mode == "HARDWARE" && fault_type == "sensor_corruption") {
        HardwareSession hardware;
        hardware.start();

        uint32_t encoderCountAddr = 0;
        uint32_t cruiseStateAddr  = 0;

        try {
            cruiseStateAddr = getSystemStateAddress(elfPath, system_state);
            encoderCountAddr = getSystemStateAddress(elfPath, address);
        }
        catch (const std::exception& e) {
            std::cerr << "[ERROR] " << e.what() << "\n";
            return -1;
        }

        std::cout << "[INFO] encoder_count @ 0x" << std::hex << encoderCountAddr << "\n"
                << "[INFO] cruise_state  @ 0x" << std::hex << cruiseStateAddr  << std::dec << "\n";

        bool testResult = hardware.sensorCorruptionTest(
            encoderCountAddr, cruiseStateAddr, duration_ms, interval_ms);

        json output;
        output["faults"] = json::array();

        json faultResult;
        faultResult["Firmware"]           = firmware;
        faultResult["Mode"]               = mode;
        faultResult["Target"]             = target;
        faultResult["id"]                 = faultID;
        faultResult["fault_type"]         = fault_type;
        faultResult["duration_ms"]        = duration_ms;
        faultResult["interval_ms"]        = interval_ms;
        faultResult["encoder_count_addr"] = [&] {
            std::stringstream ss;
            ss << "0x" << std::hex << std::uppercase << encoderCountAddr;
            return ss.str();
        }();
        faultResult["cruise_state_addr"]  = [&] {
            std::stringstream ss;
            ss << "0x" << std::hex << std::uppercase << cruiseStateAddr;
            return ss.str();
        }();
        faultResult["result"] = testResult ? "PASSED" : "FAILED";

        std::cout << "fault:\n"
                << "    Mode:               " << faultResult["Mode"]               << "\n"
                << "    Target:             " << faultResult["Target"]             << "\n"
                << "    ID:                 " << faultResult["id"]                 << "\n"
                << "    fault_type:         " << faultResult["fault_type"]         << "\n"
                << "    duration_ms:        " << faultResult["duration_ms"]        << "\n"
                << "    interval_ms:        " << faultResult["interval_ms"]        << "\n"
                << "    encoder_count_addr: " << faultResult["encoder_count_addr"] << "\n"
                << "    cruise_state_addr:  " << faultResult["cruise_state_addr"]  << "\n"
                << "    test_result:        " << faultResult["result"]             << "\n";

        output["faults"].push_back(faultResult);

        std::ofstream out("campaign_result.json");
        out << output.dump(4);
        std::cout << "[INFO] Campaign result written to campaign_result.json\n";
    }
    if (mode == "HARDWARE" && fault_type == "task_delay") {
        HardwareSession hardware;
        hardware.start();

        uint32_t samplePeriodAddr = 0;
        uint32_t cruiseStateAddr  = 0;

        try {
            samplePeriodAddr = getSystemStateAddress(elfPath, address);       // "encoder_sample_period"
            cruiseStateAddr  = getSystemStateAddress(elfPath, system_state);  // "cruise_state"
        }
        catch (const std::exception& e) {
            std::cerr << "[ERROR] " << e.what() << "\n";
            return -1;
        }

        std::cout << "[INFO] " << address      << " @ 0x" << std::hex << samplePeriodAddr << "\n"
                << "[INFO] " << system_state << " @ 0x" << std::hex << cruiseStateAddr  << std::dec << "\n";

        bool testResult = hardware.taskDelayTest(
            samplePeriodAddr, cruiseStateAddr, value, duration_ms, interval_ms);

        json output;
        output["faults"] = json::array();

        json faultResult;
        faultResult["Firmware"]            = firmware;
        faultResult["Mode"]                = mode;
        faultResult["Target"]              = target;
        faultResult["id"]                  = faultID;
        faultResult["fault_type"]          = fault_type;
        faultResult["address"]             = address;
        faultResult["system_state"]        = system_state;
        faultResult["corrupted_value"]     = value;
        faultResult["duration_ms"]         = duration_ms;
        faultResult["interval_ms"]         = interval_ms;
        faultResult["sample_period_addr"]  = [&] {
            std::stringstream ss;
            ss << "0x" << std::hex << std::uppercase << samplePeriodAddr;
            return ss.str();
        }();
        faultResult["cruise_state_addr"]   = [&] {
            std::stringstream ss;
            ss << "0x" << std::hex << std::uppercase << cruiseStateAddr;
            return ss.str();
        }();
        faultResult["result"] = testResult ? "PASSED" : "FAILED";

        std::cout << "fault:\n"
                << "    Mode:               " << faultResult["Mode"]               << "\n"
                << "    Target:             " << faultResult["Target"]             << "\n"
                << "    ID:                 " << faultResult["id"]                 << "\n"
                << "    fault_type:         " << faultResult["fault_type"]         << "\n"
                << "    address:            " << faultResult["address"]            << "\n"
                << "    system_state:       " << faultResult["system_state"]       << "\n"
                << "    corrupted_value:    " << faultResult["corrupted_value"]    << "\n"
                << "    duration_ms:        " << faultResult["duration_ms"]        << "\n"
                << "    interval_ms:        " << faultResult["interval_ms"]        << "\n"
                << "    sample_period_addr: " << faultResult["sample_period_addr"] << "\n"
                << "    cruise_state_addr:  " << faultResult["cruise_state_addr"]  << "\n"
                << "    test_result:        " << faultResult["result"]             << "\n";

        output["faults"].push_back(faultResult);

        std::ofstream out("campaign_result.json");
        out << output.dump(4);
        std::cout << "[INFO] Campaign result written to campaign_result.json\n";
    }
    if (mode == "HARDWARE" && fault_type == "bit_flip") {
        HardwareSession hardware;
        hardware.start();

        uint32_t targetAddr = 0;

        try {
            targetAddr = getSystemStateAddress(elfPath, address);  // "target_rpm"
        }
        catch (const std::exception& e) {
            std::cerr << "[ERROR] " << e.what() << "\n";
            return -1;
        }

        std::cout << "[INFO] " << address << " @ 0x"
                << std::hex << targetAddr << std::dec << "\n";

        bool testResult = hardware.bitFlip(
            targetAddr, bit_position, min, max, delay_ms);

        json output;
        output["faults"] = json::array();

        json faultResult;
        faultResult["Firmware"]     = firmware;
        faultResult["Mode"]         = mode;
        faultResult["Target"]       = target;
        faultResult["id"]           = faultID;
        faultResult["fault_type"]   = fault_type;
        faultResult["address"]      = address;
        faultResult["bit_position"] = bit_position;
        faultResult["min"]          = min;
        faultResult["max"]          = max;
        faultResult["delay_ms"]     = delay_ms;
        faultResult["resolved_addr"] = [&] {
            std::stringstream ss;
            ss << "0x" << std::hex << std::uppercase << targetAddr;
            return ss.str();
        }();
        faultResult["result"] = testResult ? "PASSED" : "FAILED";

        std::cout << "fault:\n"
                << "    Mode:         " << faultResult["Mode"]         << "\n"
                << "    Target:       " << faultResult["Target"]       << "\n"
                << "    ID:           " << faultResult["id"]           << "\n"
                << "    fault_type:   " << faultResult["fault_type"]   << "\n"
                << "    address:      " << faultResult["address"]      << "\n"
                << "    bit_position: " << faultResult["bit_position"] << "\n"
                << "    min:          " << faultResult["min"]          << "\n"
                << "    max:          " << faultResult["max"]          << "\n"
                << "    delay_ms:     " << faultResult["delay_ms"]     << "\n"
                << "    resolved_addr:" << faultResult["resolved_addr"]<< "\n"
                << "    test_result:  " << faultResult["result"]       << "\n";

        output["faults"].push_back(faultResult);

        std::ofstream out("campaign_result.json");
        out << output.dump(4);
        std::cout << "[INFO] Campaign result written to campaign_result.json\n";
    }
    if (mode == "HARDWARE" && fault_type == "pc_corruption") {
        HardwareSession hardware(host, port);
        hardware.start();

        uint32_t cruiseStateAddr = 0;

        try {
            cruiseStateAddr = getSystemStateAddress(elfPath, system_state);  // "cruise_state"
        }
        catch (const std::exception& e) {
            std::cerr << "[ERROR] " << e.what() << "\n";
            return -1;
        }

        uint32_t badPC = value; // reuse "value" field from JSON as the bad PC address

        std::cout << "[INFO] " << system_state << " @ 0x"
                << std::hex << cruiseStateAddr << std::dec << "\n"
                << "[INFO] Injecting bad PC: 0x" << std::hex << badPC << std::dec << "\n";

        bool testResult = hardware.pcCorruptionTest(badPC, cruiseStateAddr, duration_ms);

        json output;
        output["faults"] = json::array();

        json faultResult;
        faultResult["Firmware"]   = firmware;
        faultResult["Mode"]       = mode;
        faultResult["Target"]     = target;
        faultResult["id"]         = faultID;
        faultResult["fault_type"] = fault_type;
        faultResult["system_state"] = system_state;
        faultResult["bad_pc"]     = [&] {
            std::stringstream ss;
            ss << "0x" << std::hex << std::uppercase << badPC;
            return ss.str();
        }();
        faultResult["cruise_state_addr"] = [&] {
            std::stringstream ss;
            ss << "0x" << std::hex << std::uppercase << cruiseStateAddr;
            return ss.str();
        }();
        faultResult["wait_ms"] = duration_ms;
        faultResult["result"]  = testResult ? "PASSED" : "FAILED";

        std::cout << "fault:\n"
                << "    Mode:              " << faultResult["Mode"]              << "\n"
                << "    Target:            " << faultResult["Target"]            << "\n"
                << "    ID:                " << faultResult["id"]                << "\n"
                << "    fault_type:        " << faultResult["fault_type"]        << "\n"
                << "    bad_pc:            " << faultResult["bad_pc"]            << "\n"
                << "    cruise_state_addr: " << faultResult["cruise_state_addr"] << "\n"
                << "    wait_ms:           " << faultResult["wait_ms"]           << "\n"
                << "    test_result:       " << faultResult["result"]           << "\n";

        output["faults"].push_back(faultResult);

        std::ofstream out("campaign_result.json");
        out << output.dump(4);
        std::cout << "[INFO] Campaign result written to campaign_result.json\n";
    }
}
