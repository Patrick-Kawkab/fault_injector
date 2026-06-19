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

#include "FaultConfig.h"
#include "QemuSession.h"
#include "Session.h"
#include "json.hpp"   // nlohmann — only included in this file

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>


using json = nlohmann::json;
namespace fs = std::filesystem;

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

// ── ms → instruction-count conversion (QEMU mode only) ───────────────────────
//
// QEMU's plugin gives us deterministic instruction counting, which is far
// more reproducible than a wall-clock delay inside an emulated target. So for
// qemu-mode runs we convert any ms-based timing field into an approximate
// instruction count instead of passing the raw millisecond value through.
//
// insn_count ≈ duration_ms * (cpu_freq_hz / 1000), assuming ~1 instruction
// per cycle (CPI ≈ 1). This is an approximation, not a cycle-accurate value.
// Override the default via meta.cpu_freq_hz in the input JSON if you have a
// better figure (e.g. measured IPC for this firmware).
static constexpr uint64_t kDefaultQemuCpuFreqHz = 16'000'000; // lm3s6965evb default (50 MHz)

static inline uint64_t msToInsnCount(uint64_t ms, uint64_t cpuFreqHz)
{
    return (ms * cpuFreqHz) / 1000;
}

// ─────────────────────────────────

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
    bool isQemuMode,
    uint64_t cpuFreqHz)
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

    // delay_ms / duration_ms come in as wall-clock milliseconds from the GUI.
    // In qemu mode we convert them to an instruction count so the plugin can
    // trigger deterministically; in hardware mode the raw ms value is kept
    // since OpenOCD has no notion of instruction counting.
    uint64_t delay_ms    = fault.value("delay_ms", uint64_t(0));
    uint64_t duration_ms = fault.value("duration_ms", uint64_t(0));

    d.target_count =
        isQemuMode ? msToInsnCount(delay_ms, cpuFreqHz) : delay_ms;

    d.observe_window =
        isQemuMode ? msToInsnCount(duration_ms, cpuFreqHz) : duration_ms;

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

static QemuSessionConfig parseSessionConfig(const json& j, const std::string& firmwarePath) {
    const auto& meta = j["meta"];

    QemuSessionConfig cfg;
    cfg.pluginPath  = PLUGIN_PATH ;
    cfg.machine     = meta.value("machine",     std::string("lm3s6965evb"));
    cfg.cpu         = meta.value("cpu",         std::string("cortex-m4 "));
    cfg.serverPort  = meta.value("server_port", 9001);
    cfg.timeoutSecs = meta.value("timeout_secs", 30);
    cfg.firmware    = firmwarePath;

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
    const std::string inputFile  = (argc > 1) ? argv[1] : CONFIG_JSON_PATH;

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

    // Output path: prefer meta.result_file from the input JSON (set by the
    // GUI per-run), fall back to argv[2]/RESULT_JSON_PATH for standalone use.
    const std::string resultFile = config["meta"].value(
        "result_file",
        (argc > 2) ? std::string(argv[2]) : std::string(RESULT_JSON_PATH));

    // The run folder may not exist yet on first write — make sure it does.
    fs::path resultPath(resultFile);
    if (resultPath.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(resultPath.parent_path(), ec);
        if (ec) {
            std::cerr << "[main] warning: could not create result dir "
                      << resultPath.parent_path() << ": " << ec.message() << "\n";
        }
    }

    // Clear previous campaign result
    std::ofstream(resultFile, std::ios::trunc).close();

    json campaignResult;
    campaignResult["meta"] = config["meta"];
    campaignResult["faults"] = json::array();

    std::string mode = config["meta"]["mode"].get<std::string>();
    std::cout << "[main] Running in mode: " << mode << '\n';

    // Firmware path: prefer meta.run_dir from the input JSON (the GUI's
    // per-run folder, which holds the build for this specific run), fall
    // back to the fixed QEMU/HARDWARE elf directories otherwise.
    const std::string runDir       = config["meta"].value("run_dir", std::string());
    const std::string firmwareName = config["meta"]["firmware"].get<std::string>();

    // Prepare session configuration for QEMU if needed
    std::unique_ptr<QemuSessionConfig> sessionCfgPtr;
    std::string elfPath;
    if (mode == "qemu") {
        elfPath = !runDir.empty()
            ? (runDir + "/" + firmwareName)
            : (QEMU_ELF_PATH + firmwareName);
        sessionCfgPtr = std::make_unique<QemuSessionConfig>(parseSessionConfig(config, elfPath));
    }
    else if(mode == "hardware") {
        elfPath = !runDir.empty()
            ? (runDir + "/" + firmwareName)
            : (HARDWARE_ELF_PATH + firmwareName);
    }
    else {
        std::cerr << "[main] unknown mode: " << mode << "\n";
        return 1;
    }

    // Optional override for the ms→insn-count conversion factor used below.
    const uint64_t cpuFreqHz = config["meta"].value("cpu_freq_hz", kDefaultQemuCpuFreqHz);

    for (const auto& fault : config["faults"]){
        auto session = Session::create(mode, (mode == "qemu") ? sessionCfgPtr.get() : nullptr);
        FaultDescriptor desc = parseFaultDescriptor(fault, elfPath, mode == "qemu", cpuFreqHz);
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