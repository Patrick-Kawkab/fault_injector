// =============================================================================
//  QEMUSession.cpp  —  plugin-only fault injection session
//
//  No GDB.  QEMUSession is a TCP server; fault_plugin.so is the client.
//  Every fault method follows the same lifecycle:
//
//    bindServer (once in start())
//    │
//    └─ runCampaign(faultJson)
//         ├── launchQEMU()          fork + exec qemu-system-arm with -plugin
//         ├── acceptPlugin()        accept the plugin's connect()
//         ├── sendFaultDescriptor() send JSON fault, wait for "READY\n"
//         ├── [QEMU runs, plugin injects, plugin writes result file]
//         └── waitForResult()       waitpid() + parse campaign_result.json
// =============================================================================

#include "QemuSession.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <signal.h>
#include <fcntl.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>

// Simple JSON builder — avoids pulling in nlohmann just for the session layer.
// The orchestrator already has nlohmann; these helpers only build small objects.
static std::string jsonStr(const std::string& k, const std::string& v) {
    return "\"" + k + "\":\"" + v + "\"";
}
static std::string jsonU32(const std::string& k, uint32_t v) {
    char buf[32]; snprintf(buf, sizeof(buf), "\"%" "s\":%" PRIu32, k.c_str(), v);
    return std::string(buf);
}
static std::string jsonU64(const std::string& k, uint64_t v) {
    char buf[48]; snprintf(buf, sizeof(buf), "\"%" "s\":%" PRIu64, k.c_str(), v);
    return std::string(buf);
}
static std::string jsonU8(const std::string& k, uint8_t v) {
    char buf[32]; snprintf(buf, sizeof(buf), "\"%" "s\":%" PRIu8, k.c_str(), v);
    return std::string(buf);
}

// ============================================================================
//  Construction / destruction
// ============================================================================

QEMUSession::QEMUSession(const std::string& firmware,
                         const std::string& pluginPath,
                         const std::string& machine,
                         const std::string& cpu,
                         const std::string& resultFile,
                         int                serverPort)
    : firmware_(firmware),
      pluginPath_(pluginPath),
      machine_(machine),
      cpu_(cpu),
      resultFile_(resultFile),
      serverPort_(serverPort)
{}

QEMUSession::~QEMUSession() {
    stop();
}

// ============================================================================
//  start() / stop()
// ============================================================================

int QEMUSession::start() {
    // Bind the server socket once — reused for every campaign in this session
    if (!bindServer()) {
        std::cerr << "[QEMUSession] Failed to bind server on port "
                  << serverPort_ << "\n";
        return -1;
    }
    std::cout << "[QEMUSession] Server listening on port " << serverPort_ << "\n";
    return 0;
}

int QEMUSession::stop() noexcept {
    // Kill QEMU if still running
    if (qemuPid_ > 0) {
        ::kill(qemuPid_, SIGTERM);
        int status;
        ::waitpid(qemuPid_, &status, 0);
        qemuPid_ = -1;
    }
    // Close plugin connection
    if (pluginSock_ >= 0) { ::close(pluginSock_); pluginSock_ = -1; }
    // Close server socket
    if (serverSock_ >= 0) { ::close(serverSock_); serverSock_ = -1; }
    return 0;
}

// ============================================================================
//  Fault methods — identical signatures to HardwareSession
// ============================================================================

// trigger: PC == addr
bool QEMUSession::memoryCorruptionTest(uint32_t addr,
                                       uint8_t  injectedValue,
                                       uint8_t  minExpected,
                                       uint8_t  maxExpected)
{
    // Build fault descriptor JSON
    std::ostringstream js;
    js << "{"
       << jsonStr("fault_type",      "memory_corruption") << ","
       << jsonStr("trigger",         "pc")                << ","
       << jsonU32("target_addr",     addr)                << ","
       << jsonU32("inject_addr",     addr)                << ","
       << jsonU8 ("injected_value",  injectedValue)       << ","
       << jsonU8 ("min_expected",    minExpected)         << ","
       << jsonU8 ("max_expected",    maxExpected)         << ","
       << jsonStr("result_file",     resultFile_)
       << "}";

    return runCampaign(js.str());
}

// trigger: instruction count == targetCount  (newPC is the value to jump to)
// To mirror HardwareSession::setPC we accept only a uint16_t new PC value.
// The plugin will redirect execution when the instruction counter fires.
// Because setPC has no "success range" concept we always return 0.
int QEMUSession::setPC(uint16_t newPC) {
    // Default: inject at instruction 1 (immediately)
    // Caller can use the extended runCampaign path if a specific count matters
    std::ostringstream js;
    js << "{"
       << jsonStr("fault_type",    "set_pc")           << ","
       << jsonStr("trigger",       "insn_count")        << ","
       << jsonU64("target_count",  1)                   << ","
       << jsonU32("new_pc",        static_cast<uint32_t>(newPC)) << ","
       << jsonStr("result_file",   resultFile_)
       << "}";

    return runCampaign(js.str()) ? 0 : -1;
}

// trigger: instruction count == targetCount
bool QEMUSession::bitFlipTest(uint32_t addr,   uint8_t  bitPos,
                               uint8_t  minExp, uint8_t  maxExp,
                               uint64_t targetCount)
{
    std::ostringstream js;
    js << "{"
       << jsonStr("fault_type",   "bit_flip")      << ","
       << jsonStr("trigger",      "insn_count")     << ","
       << jsonU64("target_count", targetCount)      << ","
       << jsonU32("inject_addr",  addr)             << ","
       << jsonU8 ("bit_pos",      bitPos)           << ","
       << jsonU8 ("min_expected", minExp)           << ","
       << jsonU8 ("max_expected", maxExp)           << ","
       << jsonStr("result_file",  resultFile_)
       << "}";

    return runCampaign(js.str());
}

// trigger: PC == addr  (replace 2-byte Thumb instruction with NOP 0xBF00)
bool QEMUSession::instructionSkipTest(uint32_t addr) {
    std::ostringstream js;
    js << "{"
       << jsonStr("fault_type",  "instruction_skip") << ","
       << jsonStr("trigger",     "pc")               << ","
       << jsonU32("target_addr", addr)               << ","
       << jsonU32("inject_addr", addr)               << ","
       << jsonStr("result_file", resultFile_)
       << "}";

    return runCampaign(js.str());
}

// trigger: memory access to sensorAddr
bool QEMUSession::sensorCorruptionTest(uint32_t sensorAddr,
                                        uint8_t  spoofedValue,
                                        uint8_t  minExpected,
                                        uint8_t  maxExpected)
{
    std::ostringstream js;
    js << "{"
       << jsonStr("fault_type",     "sensor_corruption") << ","
       << jsonStr("trigger",        "mem_access")        << ","
       << jsonU32("sensor_addr",    sensorAddr)          << ","
       << jsonU32("inject_addr",    sensorAddr)          << ","
       << jsonU8 ("injected_value", spoofedValue)        << ","
       << jsonU8 ("min_expected",   minExpected)         << ","
       << jsonU8 ("max_expected",   maxExpected)         << ","
       << jsonStr("result_file",    resultFile_)
       << "}";

    return runCampaign(js.str());
}

// ============================================================================
//  Internal helpers
// ============================================================================

bool QEMUSession::bindServer() {
    serverSock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock_ < 0) return false;

    // Allow immediate reuse after restart
    int opt = 1;
    ::setsockopt(serverSock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(serverPort_);

    if (::bind(serverSock_,
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(serverSock_);
        serverSock_ = -1;
        return false;
    }

    ::listen(serverSock_, 1);
    return true;
}

bool QEMUSession::launchQEMU() {
    // qemu-system-arm                                        \
    //   -machine <machine> -cpu <cpu>                        \
    //   -nographic -kernel <firmware>                        \
    //   -plugin <pluginPath>,server=localhost:<serverPort>   \
    //   > /tmp/qemu.log 2>&1
    std::ostringstream cmd;
    cmd << "qemu-system-arm"
        << " -machine "  << machine_
        << " -cpu "      << cpu_
        << " -nographic"
        << " -kernel "   << firmware_
        << " -plugin "   << pluginPath_
                         << ",server=localhost:" << serverPort_
        << " > /tmp/qemu_session.log 2>&1";

    qemuPid_ = ::fork();
    if (qemuPid_ < 0) {
        std::cerr << "[QEMUSession] fork() failed\n";
        return false;
    }
    if (qemuPid_ == 0) {
        ::execl("/bin/sh", "sh", "-c", cmd.str().c_str(), nullptr);
        ::_exit(127);
    }
    return true;
}

bool QEMUSession::acceptPlugin() {
    // Set a 5-second timeout on the accept
    timeval tv{ .tv_sec = 5, .tv_usec = 0 };
    ::setsockopt(serverSock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in peer{};
    socklen_t   peerLen = sizeof(peer);
    pluginSock_ = ::accept(serverSock_,
                           reinterpret_cast<sockaddr*>(&peer), &peerLen);
    if (pluginSock_ < 0) {
        std::cerr << "[QEMUSession] accept() timed out — plugin did not connect\n";
        return false;
    }

    // Disable Nagle for low-latency exchange
    int flag = 1;
    ::setsockopt(pluginSock_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    return true;
}

bool QEMUSession::sendFaultDescriptor(const std::string& json) {
    // Send JSON line
    std::string line = json + "\n";
    if (::write(pluginSock_, line.c_str(), line.size()) < 0) {
        std::cerr << "[QEMUSession] write fault descriptor failed\n";
        return false;
    }

    // Wait for "READY\n" ACK from plugin (means it parsed the descriptor)
    char buf[64] = {0};
    ssize_t n = ::read(pluginSock_, buf, sizeof(buf) - 1);
    if (n <= 0) {
        std::cerr << "[QEMUSession] no ACK from plugin\n";
        return false;
    }

    return std::string(buf, n).find("READY") != std::string::npos;
}

bool QEMUSession::waitForResult() {
    // Block until QEMU exits
    int status = 0;
    ::waitpid(qemuPid_, &status, 0);
    qemuPid_ = -1;

    // Close the plugin socket — QEMU is gone
    if (pluginSock_ >= 0) { ::close(pluginSock_); pluginSock_ = -1; }

    // Parse campaign_result.json written by the plugin
    std::ifstream f(resultFile_);
    if (!f.is_open()) {
        std::cerr << "[QEMUSession] result file not found: " << resultFile_ << "\n";
        return false;
    }

    // Minimal parse: look for "result":"PASSED"
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    return content.find("\"PASSED\"") != std::string::npos;
}

bool QEMUSession::runCampaign(const std::string& faultJson) {
    // 1. Launch QEMU — it will load the plugin, plugin will connect to us
    if (!launchQEMU()) return false;

    // Small delay to let QEMU initialise before accepting
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 2. Accept plugin connection
    if (!acceptPlugin()) {
        ::kill(qemuPid_, SIGTERM);
        int st; ::waitpid(qemuPid_, &st, 0);
        qemuPid_ = -1;
        return false;
    }

    // 3. Send fault descriptor; plugin ACKs with "READY"
    if (!sendFaultDescriptor(faultJson)) {
        ::kill(qemuPid_, SIGTERM);
        int st; ::waitpid(qemuPid_, &st, 0);
        qemuPid_ = -1;
        return false;
    }

    std::cout << "[QEMUSession] Fault injected, waiting for QEMU to finish...\n";

    // 4. Wait for QEMU to exit and read the result
    return waitForResult();
}