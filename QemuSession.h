#ifndef QEMU_SESSION_H
#define QEMU_SESSION_H

// =============================================================================
//  QEMUSession  (plugin-only approach, no GDB)
//
//  Communication flow
//  ──────────────────
//  1. QEMUSession::start()
//       - binds a TCP server socket  (default port 9001)
//       - launches QEMU subprocess with fault_plugin.so
//       - accepts the plugin's connection
//       - sends a JSON fault descriptor to the plugin
//
//  2. fault_plugin.so  (running inside QEMU's process)
//       - connects back to QEMUSession's TCP server
//       - receives the JSON fault descriptor
//       - watches TCG callbacks for the right trigger condition
//       - performs the injection directly via QEMU plugin memory/CPU APIs
//       - sends a JSON result line back over TCP
//       - writes campaign_result.json on QEMU exit
//
//  Trigger strategy per fault type
//  ────────────────────────────────
//  memory_corruption  →  PC == target_addr
//  instruction_skip   →  PC == target_addr
//  bit_flip           →  instruction count == target_count
//  set_pc             →  instruction count == target_count
//  sensor_corruption  →  any memory access touching sensor_addr
//
//  Public interface is identical to HardwareSession — the orchestrator
//  can swap session types without any other changes.
// =============================================================================

#include "Session.h"
#include <string>
#include <cstdint>
#include <sys/types.h>   // pid_t

class QEMUSession : public Session {
public:
    // -------------------------------------------------------------------------
    //  firmware   : path to ELF / flat binary to run under QEMU
    //  pluginPath : path to fault_plugin.so
    //  machine    : QEMU -machine value  (e.g. "lm3s6965evb" for Cortex-M3)
    //  cpu        : QEMU -cpu value      (e.g. "cortex-m3")
    //  resultFile : where the plugin writes the final campaign_result.json
    //  serverPort : TCP port QEMUSession binds; plugin connects to this
    // -------------------------------------------------------------------------
    explicit QEMUSession(const std::string& firmware,
                         const std::string& pluginPath  = "./fault_plugin.so",
                         const std::string& machine     = "lm3s6965evb",
                         const std::string& cpu         = "cortex-m3",
                         const std::string& resultFile  = "./campaign_result.json",
                         int                serverPort   = 9001);

    ~QEMUSession();

    // ── Session interface — identical signatures to HardwareSession ──────────
    int  start()          override;   // bind server, ready to accept plugin
    int  stop()  noexcept override;   // kill QEMU, close sockets

    // trigger: PC == addr
    bool memoryCorruptionTest(uint32_t addr,
                              uint8_t  injectedValue,
                              uint8_t  minExpected,
                              uint8_t  maxExpected) override;

    // trigger: instruction count == targetCount  (set new PC value)
    int  setPC(uint16_t PC) override;

    // ── Extra fault primitives ───────────────────────────────────────────────

    // trigger: instruction count == targetCount  (flip bit at addr)
    bool bitFlipTest(uint32_t addr,   uint8_t  bitPos,
                     uint8_t  minExp, uint8_t  maxExp,
                     uint64_t targetCount);

    // trigger: PC == addr  (replace instruction with NOP)
    bool instructionSkipTest(uint32_t addr);

    // trigger: any memory access to sensorAddr
    bool sensorCorruptionTest(uint32_t sensorAddr,
                              uint8_t  spoofedValue,
                              uint8_t  minExpected,
                              uint8_t  maxExpected);

private:
    // ── Config ───────────────────────────────────────────────────────────────
    std::string firmware_;
    std::string pluginPath_;
    std::string machine_;
    std::string cpu_;
    std::string resultFile_;
    int         serverPort_;

    // ── Runtime ──────────────────────────────────────────────────────────────
    int         serverSock_  = -1;   // bound listening socket
    int         pluginSock_  = -1;   // accepted connection from plugin
    pid_t       qemuPid_     = -1;   // QEMU subprocess PID

    // ── Internal helpers ─────────────────────────────────────────────────────

    // Bind the server socket (called once in start())
    bool bindServer();

    // Fork + exec QEMU with the plugin loaded
    bool launchQEMU();

    // Accept the single TCP connection the plugin makes at startup
    bool acceptPlugin();

    // Send the JSON fault descriptor to the plugin, wait for "READY" ACK
    bool sendFaultDescriptor(const std::string& json);

    // Block until QEMU exits; parse campaign_result.json; return pass/fail
    bool waitForResult();

    // One-shot wrapper used by every fault method:
    //   launchQEMU → acceptPlugin → sendFaultDescriptor → waitForResult
    bool runCampaign(const std::string& faultJson);
};

#endif // QEMU_SESSION_H