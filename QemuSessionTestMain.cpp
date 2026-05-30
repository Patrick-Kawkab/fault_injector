#define CONFIG_JSON_PATH   "./Orchestrator/Input.json"
#define ELF_FILE_DIR       "./tiva_c/"

#include <iostream>
#include <fstream>
#include <sstream>

#include "QemuSession.h"
#include "json.hpp"

using json = nlohmann::json;

/* -------- Symbol → Address -------- */
uint32_t resolveAddress(const std::string& elfPath,
                        const std::string& symbol)
{
    std::string cmd = "arm-none-eabi-nm " + elfPath + " | grep " + symbol;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("nm failed");

    char buffer[256];
    std::string result;

    while (fgets(buffer, sizeof(buffer), pipe))
        result += buffer;

    pclose(pipe);

    if (result.empty())
        throw std::runtime_error("Symbol not found");

    std::stringstream ss(result);
    std::string addrStr;
    ss >> addrStr;

    return std::stoul(addrStr, nullptr, 16);
}

int main() {
    std::cout << "[INFO] Fault Injector (QEMU Mode)\n";

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

    const int faultID            = fault["id"];
    const std::string firmware   = fault["Firmware"];
    const std::string mode       = fault["Mode"];
    const std::string target     = fault["Target"];
    const std::string fault_type = fault["fault_type"];
    const uint8_t max            = fault["max"];
    const uint8_t min            = fault["min"];
    const std::string addressStr = fault["address"];
    const uint8_t value          = fault["value"];

    std::string elfPath = std::string(ELF_FILE_DIR) + firmware;

    /* ---------------- QEMU MODE ---------------- */
    if (mode == "QEMU") {

        uint32_t addr = 0;

        try {
            addr = resolveAddress(elfPath, addressStr);
            std::cout << "[INFO] Address: 0x"
                      << std::hex << addr << std::dec << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Address resolution failed: "
                      << e.what() << "\n";
            return -1;
        }

        /* 🔥 IMPORTANT: constructor requires firmware */
        QEMUSession qemu(elfPath);

        if (qemu.start() != 0) {
            std::cerr << "[ERROR] Failed to start QEMU session\n";
            return -1;
        }

        bool result = false;

        try {
            if (fault_type == "memory_corruption") {
                result = qemu.memoryCorruptionTest(
                    addr,
                    value,
                    min,
                    max
                );
            }
            else if (fault_type == "instruction_skip") {
                result = qemu.instructionSkipTest(addr);
            }
            else if (fault_type == "bit_flip") {
                uint8_t bitPos = fault["bit"];
                uint64_t count = fault["count"];

                result = qemu.bitFlipTest(
                    addr,
                    bitPos,
                    min,
                    max,
                    count
                );
            }
            else if (fault_type == "sensor_corruption") {
                result = qemu.sensorCorruptionTest(
                    addr,
                    value,
                    min,
                    max
                );
            }
            else {
                std::cerr << "[ERROR] Unknown fault type\n";
                return -1;
            }

        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Campaign failed: "
                    << e.what() << "\n";
            qemu.stop();
            return -1;
        }

        qemu.stop();

        /* ---------------- Output JSON ---------------- */
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
        faultResult["address"]    = addressStr;
        faultResult["value"]      = value;
        faultResult["result"]     = result ? "PASSED" : "FAILED";

        output["faults"].push_back(faultResult);

        std::ofstream out("Output.json");
        out << output.dump(4);

        std::cout << "[INFO] Done → "
                  << (result ? "PASSED" : "FAILED") << "\n";
    }

    return 0;
}