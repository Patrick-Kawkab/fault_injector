// =============================================================================
//  main.cpp  —  sole owner of all JSON in the project
//
//  Responsibilities:
//    1. Read  Input.json          → fill FaultDescriptor + QemuSessionConfig
//    2. Run   QEMUSession         → get FaultResult back
//    3. Write campaign_result.json from FaultDescriptor + FaultResult
//
//  No JSON anywhere else in the codebase.
// =============================================================================

#include "FaultConfig.h"
#include "QemuSession.h"
#include "json.hpp"   // nlohmann — only included in this file

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

#define CONFIG_JSON_PATH   "./Qemu_Test/Input.json"  
#define RESULT_JSON_PATH   "./Qemu_Test/campaign_result.json"
#define ELF_FILE           "./Cruise_Control/Qemu/Corrected/main.elf"
#define ELF_PATH           "./Cruise_Control/Qemu/Corrected/"

using json = nlohmann::json;

// ── Lookup tables — string ↔ enum ────────────────────────────────────────────

static const std::unordered_map<std::string, FaultType> kFaultTypeMap = {
    { "memory_corruption", FAULT_MEMORY_CORRUPTION },
    { "instruction_skip",  FAULT_INSTRUCTION_SKIP  },
    { "bit_flip",          FAULT_BIT_FLIP          },
    { "set_pc",            FAULT_SET_PC            },
    { "sensor_corruption", FAULT_SENSOR_CORRUPTION },
};

static const std::unordered_map<std::string, TriggerType> kTriggerMap = {
    { "pc",         TRIGGER_PC         },
    { "insn_count", TRIGGER_INSN_COUNT },
    { "mem_access", TRIGGER_MEM_ACCESS },
};

static const std::unordered_map<FaultType, std::string> kFaultTypeNames = {
    { FAULT_MEMORY_CORRUPTION, "memory_corruption" },
    { FAULT_INSTRUCTION_SKIP,  "instruction_skip"  },
    { FAULT_BIT_FLIP,          "bit_flip"          },
    { FAULT_SET_PC,            "set_pc"            },
    { FAULT_SENSOR_CORRUPTION, "sensor_corruption" },
};

static const std::unordered_map<TriggerType, std::string> kTriggerNames = {
    { TRIGGER_PC,         "pc"         },
    { TRIGGER_INSN_COUNT, "insn_count" },
    { TRIGGER_MEM_ACCESS, "mem_access" },
};

// ─────────────────────────────────

uint32_t getSystemStateAddress(const std::string& elfPath ,const std::string& address)
{
    printf("[INFO] Getting system state address for %s in %s\n", address.c_str(), elfPath.c_str());
    if(address.empty()) {
        return 0; // Return 0 if the address string is empty
    }
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

// ── JSON → FaultDescriptor ────────────────────────────────────────────────────

static FaultDescriptor parseFaultDescriptor(const json& j ,const std::string& elfPath) {
    FaultDescriptor d{};

    d.fault_type     = kFaultTypeMap.at(j.at("fault_type").get<std::string>());
    d.trigger        = kTriggerMap  .at(j.at("trigger")   .get<std::string>());
    d.injected_value = j.value("injected_value", 0u);
    d.bit_pos        = j.value("bit_pos",        0u);
    d.target_addr    = getSystemStateAddress(elfPath, j.at("target_addr").get<std::string>());//
    d.inject_addr    = getSystemStateAddress(elfPath, j.at("inject_addr").get<std::string>());//
    d.sensor_addr    = getSystemStateAddress(elfPath, j.at("sensor_addr").get<std::string>());//
    d.new_pc         = getSystemStateAddress(elfPath,      j.at("new_pc").get<std::string>());//
    d.target_count   = j.value("target_count",   uint64_t(0));
    d.min_expected   = j.value("min_expected",   0u);
    d.max_expected   = j.value("max_expected",   0u);
    return d;
}

// ── JSON → QemuSessionConfig ──────────────────────────────────────────────────

static QemuSessionConfig parseSessionConfig(const json& j) {
    QemuSessionConfig cfg;
    cfg.firmware    = j.at   ("firmware")   .get<std::string>();
    cfg.pluginPath  = j.value("plugin_path", std::string("./fault_plugin.so"));
    cfg.machine     = j.value("machine",     std::string("lm3s6965evb"));
    cfg.cpu         = j.value("cpu",         std::string("cortex-m3"));
    cfg.serverPort  = j.value("server_port", 9001);
    cfg.timeoutSecs = j.value("timeout_secs", 30);
    return cfg;
}

// ── FaultDescriptor + FaultResult → campaign_result.json ─────────────────────

static void writeResult(const std::string&    resultFile,
                        const FaultDescriptor& desc,
                        const FaultResult&     result)
{
    json out = {
        { "fault_type",     kFaultTypeNames.at(static_cast<FaultType>(desc.fault_type))  },
        { "trigger",        kTriggerNames  .at(static_cast<TriggerType>(desc.trigger))   },
        { "target_addr",    desc.target_addr    },
        { "inject_addr",    desc.inject_addr    },
        { "injected_value", desc.injected_value },
        { "bit_pos",        desc.bit_pos        },
        { "new_pc",         desc.new_pc         },
        { "sensor_addr",    desc.sensor_addr    },
        { "target_count",   desc.target_count   },
        { "min_expected",   desc.min_expected   },
        { "max_expected",   desc.max_expected   },
        { "injected",       result.injected != 0 },
        { "insn_count",     result.insn_count    },
        { "result",         result.passed ? "PASSED" : "FAILED" },
    };

    std::ofstream f(resultFile);
    if (!f.is_open()) {
        std::cerr << "[main] cannot open result file: " << resultFile << "\n";
        return;
    }
    f << out.dump(4) << "\n";
    std::cout << "[main] result written to " << resultFile
              << "  (" << (result.passed ? "PASSED" : "FAILED") << ")\n";
}

// ============================================================================
//  Entry point
// ============================================================================

int main(int argc, char* argv[]) {
    const std::string inputFile  = (argc > 1) ? argv[1] : CONFIG_JSON_PATH;
    const std::string resultFile = (argc > 2) ? argv[2] : RESULT_JSON_PATH;

    // 1. Parse input
    std::ifstream ifs(inputFile);
    if (!ifs.is_open()) {
        std::cerr << "[main] cannot open " << inputFile << "\n";
        return 1;
    }
    json input;
    try { input = json::parse(ifs); }
    catch (const json::exception& e) {
        std::cerr << "[main] JSON parse error: " << e.what() << "\n";
        return 1;
    }

    QemuSessionConfig sessionCfg = parseSessionConfig(input);

    /* Construct full ELF path */
    std::string elfPath = sessionCfg.firmware;
    FaultDescriptor desc = parseFaultDescriptor(input ,elfPath );

    // 2. Run session
    QEMUSession session(sessionCfg);
    if (session.start() != 0) {
        std::cerr << "[main] session.start() failed\n";
        return 1;
    }

    FaultResult result{};
    switch (static_cast<FaultType>(desc.fault_type)) {
        case FAULT_MEMORY_CORRUPTION: result = session.memoryCorruptionTest(desc); break;
        case FAULT_INSTRUCTION_SKIP:  result = session.instructionSkipTest(desc);  break;
        case FAULT_BIT_FLIP:          result = session.bitFlipTest(desc);          break;
        case FAULT_SET_PC:            result = session.setPC(desc);                break;
        case FAULT_SENSOR_CORRUPTION: result = session.sensorCorruptionTest(desc); break;
        default:
            std::cerr << "[main] unknown fault_type\n";
            return 1;
    }

    // 3. Write result — the only JSON write in the project
    writeResult(resultFile, desc, result);

    return result.passed ? 0 : 1;
}