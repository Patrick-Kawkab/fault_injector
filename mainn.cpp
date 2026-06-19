// =============================================================================
//  main.cpp  —  sole owner of all JSON in the project
//
//  Responsibilities:
//    1. Read  Input.json          → fill FaultDescriptor + QemuSessionConfig
//    2. Run   Session             → get FaultResult back
//    3. Write campaign_result.json from FaultDescriptor + FaultResult
//
//  No JSON anywhere else in the codebase.
// =============================================================================

#define CONFIG_JSON_PATH        "./Input.json"  
#define RESULT_JSON_PATH        "./campaign_result.json"
#define QEMU_ELF_PATH           "./Cruise_Control/Qemu/Corrected/"
#define HARDWARE_ELF_PATH       "./Cruise_Control/Hardware/Corrected/"
#define PLUGIN_PATH             "./Qemu_Plugin/fault_plugin.so"  

// --- constants ---
constexpr uint32_t CLOCK_HZ = 16'000'000;
constexpr double   IPC      = 1.0;

#include "FaultConfig.h"
#include "QemuSession.h"
#include "Session.h"
#include "json.hpp"   // nlohmann — only included in this file

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>


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

// ────────helpers─────────────────────────

static uint64_t msToInstructions(uint64_t ms)
{
    return static_cast<uint64_t>(
        (ms / 1000.0) * CLOCK_HZ * IPC);
}

uint32_t getSystemStateAddress(const std::string& elfPath, const std::string& address)
{
    printf("[INFO] Getting system state address for %s in %s\n", address.c_str(), elfPath.c_str());
    if (address.empty()) {
        return 0;
    }

    std::string cmd = "arm-none-eabi-nm " + elfPath + " 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run nm");
    }

    char line[256] = {0};
    uint32_t result = 0;
    bool found = false;

    while (fgets(line, sizeof(line), pipe)) {
        // Each nm line: "20000030 b sim_rpm.0"
        char addr_str[64] = {0};
        char type_str[8]  = {0};
        char name_str[128] = {0};

        if (sscanf(line, "%63s %7s %127s", addr_str, type_str, name_str) != 3)
            continue;

        // Match exact name OR name with .N suffix (static locals)
        std::string sym(name_str);
        bool exact  = (sym == address);
        bool suffix = (sym.rfind(address + ".", 0) == 0); // starts with "address."

        if (exact || suffix) {
            result = static_cast<uint32_t>(std::stoul(addr_str, nullptr, 16));
            found  = true;
            printf("[INFO] Resolved %s -> %s -> 0x%08X\n",
                   address.c_str(), name_str, result);
            break;
        }
    }

    pclose(pipe);

    if (!found)
        throw std::runtime_error("system_state not found: " + address);

    return result;
}

// ── JSON → FaultDescriptor ────────────────────────────────────────────────────

static FaultDescriptor parseFaultDescriptor(
    const json& fault,
    const std::string& elfPath,
    const std::string& mode)
{
    FaultDescriptor d{};

    d.fault_type =
        kFaultTypeMap.at(
            fault.at("fault_type").get<std::string>());

    d.trigger =
        kTriggerMap.at(
            fault.at("trigger").get<std::string>());

    std::string target_sym =
        fault.value("address", "");

    d.target_addr =
        target_sym.empty()
            ? 0
            : getSystemStateAddress(elfPath, target_sym);

    std::string new_pc_sym =
        fault.value("address", "");

    d.new_pc =
        new_pc_sym.empty()
            ? 0
            : getSystemStateAddress(elfPath, new_pc_sym);

    std::string variable =
        fault.value("variable", "");

    d.inject_addr =
        variable.empty()
            ? 0
            : getSystemStateAddress(elfPath, variable);

    std::string system_state =
        fault.value("system_state", "");

    d.sensor_addr =
        system_state.empty()
            ? 0
            : getSystemStateAddress(elfPath, system_state);

    d.target_count =
        mode == "qemu"
            ? msToInstructions(fault.value("delay_ms",    uint64_t(0)))
            : fault.value("delay_ms",    uint64_t(0));

    d.observe_window =
        mode == "qemu"
            ? msToInstructions(fault.value("duration_ms", uint64_t(0)))
            : fault.value("duration_ms", uint64_t(0));

    d.injected_value =
        fault.value("value", 0u);

    d.bit_pos =
        fault.value("bit_position", 0u);

    d.min_expected =
        fault.value("min", 0u);

    d.max_expected =
        fault.value("max", 0u);

    return d;
}


// ── JSON → QemuSessionConfig ──────────────────────────────────────────────────

static QemuSessionConfig parseSessionConfig(const json& j ) {
    const auto& meta = j["meta"];

    QemuSessionConfig cfg;
    cfg.pluginPath  = PLUGIN_PATH ;
    cfg.machine     = meta.value("machine",     std::string("lm3s6965evb"));
    cfg.cpu         = meta.value("cpu",         std::string("cortex-m4 "));
    cfg.serverPort  = meta.value("server_port", 9001);
    cfg.timeoutSecs = meta.value("timeout_secs", 30);
    cfg.firmware =
    QEMU_ELF_PATH + meta.at("firmware").get<std::string>();

    printf("[INFO] QEMU session config:\n");
    printf("  firmware    : %s\n", cfg.firmware.c_str());
    printf("  pluginPath  : %s\n", cfg.pluginPath.c_str());
    printf("  machine     : %s\n", cfg.machine.c_str());
    printf("  cpu         : %s\n", cfg.cpu.c_str());
    printf("  serverPort  : %d\n", cfg.serverPort);
    printf("  timeoutSecs : %d\n", cfg.timeoutSecs);

    return cfg;

}

// ── FaultDescriptor + FaultResult → campaign_result.json ─────────────────────

static void writeResult(
    const std::string& resultFile,
    const json& meta,
    const json& fault,
    const FaultResult& result)
{
    json output;

    // Load existing campaign result if present
    std::ifstream in(resultFile);
    if (in.is_open())
    {
        try
        {
            in >> output;
        }
        catch (...)
        {
            output = json{};
        }
    }

    // Initialize file structure on first write
    if (!output.contains("meta"))
    {
        output["meta"] = meta;
        output["faults"] = json::array();
    }

    output["faults"].push_back({
        {"id",         fault.value("id", 0)},
        {"fault_type", fault.value("fault_type", "")},
        {"variable",   fault.value("variable", "")},
        {"value",      fault.value("value", 0)},
        {"min",        fault.value("min", 0)},
        {"max",        fault.value("max", 0)},
        {"result",     result.passed ? "PASS" : "FAIL"}
    });

    std::ofstream out(resultFile);
    if (!out.is_open())
    {
        std::cerr << "[main] cannot open result file: "
                  << resultFile << "\n";
        return;
    }

    out << output.dump(4) << "\n";

    std::cout << "[main] fault "
              << fault.value("id", 0)
              << " written ("
              << (result.passed ? "PASS" : "FAIL")
              << ")\n";
}

// ============================================================================
//  Entry point
// ============================================================================

int main(int argc ,char* argv[]){
    const std::string inputFile  = (argc > 1) ? argv[1] : NULL;

    std::ifstream ifs(inputFile);
    if (!ifs.is_open()) {
        std::cerr << "[main] cannot open " << inputFile << "\n";
        return 1;
    }

    json config;
    try { config = json::parse(ifs); }
    catch (const json::exception& e) {
        std::cerr << "[main] JSON parse error: " << e.what() << "\n";
        return 1;
    }

    json campaignResult;
    campaignResult["meta"] = config["meta"];
    campaignResult["faults"] = json::array();

    const std::string resultFile =  config["meta"]["xxxx"].get<std::string>();//xxxx -> el mkan elli 7atito feh el result

    std::string mode = config["meta"]["mode"].get<std::string>();
    std::cout << "[main] Running in mode: " << mode << '\n';

    // Prepare session configuration for QEMU if needed
    std::unique_ptr<QemuSessionConfig> sessionCfgPtr;
    std::string elfPath;
    if (mode == "qemu") {
        elfPath = QEMU_ELF_PATH  + config["meta"]["firmware"].get<std::string>();
        sessionCfgPtr = std::make_unique<QemuSessionConfig>(parseSessionConfig(config));
    }

    else if(mode == "hardware") {
        elfPath = HARDWARE_ELF_PATH  + config["meta"]["firmware"].get<std::string>();
    }
    else {
        std::cerr << "[main] unknown mode: " << mode << "\n";
        return 1;
    }

    for (const auto& fault : config["faults"]){
        auto session = Session::create(mode, (mode == "qemu") ? sessionCfgPtr.get() : nullptr);
        FaultDescriptor desc = parseFaultDescriptor(fault, elfPath, mode);
        if (session->start() != 0) {
            std::cerr << "[main] session.start() failed\n";
            return 1;
        }

        FaultResult result {};

        switch (static_cast<FaultType>(desc.fault_type)) {
        case FAULT_MEMORY_CORRUPTION: result = session->memoryCorruptionTest(desc); break;
        case FAULT_INSTRUCTION_SKIP:  result = session->Task_delay(desc);           break;
        case FAULT_BIT_FLIP:          result = session->bitFlipTest(desc);          break;
        case FAULT_SET_PC:            result = session->setPC(desc);                break;
        case FAULT_SENSOR_CORRUPTION: result = session->sensorCorruptionTest(desc); break;
        default:
            std::cerr << "[main] unknown fault_type\n";
            return 1;
        }
        
        writeResult(
            resultFile,
            config["meta"],
            fault,
            result);
        

        session->stop();
    }
    return 0;
}