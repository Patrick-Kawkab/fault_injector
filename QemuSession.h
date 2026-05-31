#ifndef QEMU_SESSION_H
#define QEMU_SESSION_H

// =============================================================================
//  QemuSession.h  —  plugin-only fault injection session (no GDB, no JSON)
//
//  Communication flow
//  ──────────────────
//  1. main fills QemuSessionConfig + FaultDescriptor structs
//  2. QEMUSession::start()  — binds TCP server socket
//  3. fault method called   — launchQEMU → acceptPlugin →
//                             sendDescriptor (binary) → recvResult (binary)
//  4. FaultResult returned to main
//  5. main writes campaign_result.json
//
//  Wire protocol (both directions are raw binary structs):
//    host → plugin : write(sock, &FaultDescriptor, sizeof(FaultDescriptor))
//    plugin → host : write(sock, &FaultResult,     sizeof(FaultResult))
// =============================================================================

#include "Session.h"
#include "FaultConfig.h"
#include <string>
#include <sys/types.h>

// ── QEMU session configuration ────────────────────────────────────────────────
struct QemuSessionConfig {
    std::string firmware;
    std::string pluginPath  = "./fault_plugin.so";
    std::string machine     = "lm3s6965evb";
    std::string cpu         = "cortex-m3";
    int         serverPort  = 9001;
    int         timeoutSecs = 30;
};

class QEMUSession : public Session {
public:
    explicit QEMUSession(const QemuSessionConfig& cfg);
    ~QEMUSession();

    // ── Session lifecycle ────────────────────────────────────────────────────
    int  start()         override;   // bind server socket
    int  stop() noexcept override;   // kill QEMU, close sockets

    // ── Fault methods — all delegate to runCampaign ──────────────────────────
    FaultResult memoryCorruptionTest(const FaultDescriptor& desc) override;
    FaultResult bitFlipTest         (const FaultDescriptor& desc) override;
    FaultResult instructionSkipTest (const FaultDescriptor& desc) override;
    FaultResult sensorCorruptionTest(const FaultDescriptor& desc) override;
    FaultResult setPC               (const FaultDescriptor& desc) override;

private:
    QemuSessionConfig cfg_;

    int   serverSock_ = -1;
    int   pluginSock_ = -1;
    pid_t qemuPid_    = -1;

    // ── Internal helpers ─────────────────────────────────────────────────────
    bool        bindServer();
    bool        launchQEMU();
    bool        acceptPlugin();
    bool        sendDescriptor(const FaultDescriptor& desc);
    FaultResult recvResult();
    FaultResult runCampaign(const FaultDescriptor& desc);
};

#endif // QEMU_SESSION_H