// =============================================================================
//  QemuSession.cpp  —  plugin-only fault injection session
//
//  No JSON anywhere.  Wire protocol is raw binary structs:
//
//    sendDescriptor()  →  write(sock, &FaultDescriptor, sizeof(FaultDescriptor))
//    recvResult()      ←  read (sock, &FaultResult,     sizeof(FaultResult))
//
//  Every fault method:
//    runCampaign(desc)
//      ├── launchQEMU()       fork + exec qemu-system-arm with -plugin
//      ├── acceptPlugin()     accept plugin's TCP connect()
//      ├── sendDescriptor()   send FaultDescriptor binary, wait for ACK byte
//      ├── [QEMU runs, plugin injects, plugin evaluates]
//      ├── recvResult()       read FaultResult binary from plugin
//      └── stop()             kill QEMU, close sockets
// =============================================================================

#include "QemuSession.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include <cstring>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>

// ── ACK byte sent by plugin after it receives the descriptor ─────────────────
static constexpr uint8_t ACK_READY = 0xAC;

// ── QEMU log path — UART output + plugin stderr go here ──────────────────────
static constexpr const char* QEMU_LOG = "/tmp/qemu_session.log";

// ── Fault type name helper for prints ────────────────────────────────────────
static const char* faultTypeName(uint8_t ft) {
    switch (ft) {
        case FAULT_MEMORY_CORRUPTION: return "memory_corruption";
        case FAULT_INSTRUCTION_SKIP:  return "instruction_skip";
        case FAULT_BIT_FLIP:          return "bit_flip";
        case FAULT_SET_PC:            return "set_pc";
        case FAULT_SENSOR_CORRUPTION: return "sensor_corruption";
        default:                      return "unknown";
    }
}

static const char* triggerName(uint8_t tr) {
    switch (tr) {
        case TRIGGER_PC:         return "pc";
        case TRIGGER_INSN_COUNT: return "insn_count";
        case TRIGGER_MEM_ACCESS: return "mem_access";
        default:                 return "unknown";
    }
}

// ── Print QEMU log to terminal, filtering out noisy per-instruction TB lines ─
static void printQemuLog() {
    std::ifstream log(QEMU_LOG);
    if (!log.is_open()) {
        std::cerr << "[QEMUSession] could not open " << QEMU_LOG << "\n";
        return;
    }
    std::cout << "\n[QEMUSession] ---- QEMU log (UART + plugin output) ----------\n";
    std::string line;
    while (std::getline(log, line)) {
        // Skip the noisy per-instruction TB lines e.g. "tb_trans:   [0]  PC=..."
        // Keep everything else: UART output, plugin key events, inject lines
        if (line.find("tb_trans:   [") != std::string::npos) continue;
        std::cout << "  " << line << "\n";
    }
    std::cout << "[QEMUSession] ---- end of QEMU log ---------------------------\n\n";
}

// ============================================================================
//  Construction / destruction
// ============================================================================

QEMUSession::QEMUSession(const QemuSessionConfig& cfg)
    : cfg_(cfg)
{}

QEMUSession::~QEMUSession() {
    stop();
}

// ============================================================================
//  start() / stop()
// ============================================================================

int QEMUSession::start() {
    std::cout << "\n[QEMUSession] ============================================\n";
    std::cout << "[QEMUSession] Initialising session\n";
    std::cout << "[QEMUSession]   firmware   : " << cfg_.firmware   << "\n";
    std::cout << "[QEMUSession]   plugin     : " << cfg_.pluginPath << "\n";
    std::cout << "[QEMUSession]   machine    : " << cfg_.machine    << "\n";
    std::cout << "[QEMUSession]   cpu        : " << cfg_.cpu        << "\n";
    std::cout << "[QEMUSession]   port       : " << cfg_.serverPort << "\n";
    std::cout << "[QEMUSession]   timeout    : " << cfg_.timeoutSecs << "s\n";
    std::cout << "[QEMUSession] ============================================\n";

    if (!bindServer()) {
        std::cerr << "[QEMUSession] ERROR: failed to bind server on port "
                  << cfg_.serverPort << "\n";
        return -1;
    }

    std::cout << "[QEMUSession] TCP server bound on port "
              << cfg_.serverPort << " — waiting for plugin\n";
    return 0;
}

int QEMUSession::stop() noexcept {
    if (qemuPid_ > 0) {
        std::cout << "[QEMUSession] Sending SIGTERM to QEMU PID=" << qemuPid_ << "\n";
        ::kill(qemuPid_, SIGTERM);
        int status;
        ::waitpid(qemuPid_, &status, 0);
        std::cout << "[QEMUSession] QEMU exited  status=" << status << "\n";
        qemuPid_ = -1;
    }
    if (pluginSock_ >= 0) {
        ::close(pluginSock_);
        pluginSock_ = -1;
        std::cout << "[QEMUSession] Plugin socket closed\n";
    }
    return 0;
}

// ============================================================================
//  Fault methods — all delegate to runCampaign
// ============================================================================

FaultResult QEMUSession::memoryCorruptionTest(const FaultDescriptor& desc) {
    return runCampaign(desc);
}

FaultResult QEMUSession::bitFlipTest(const FaultDescriptor& desc) {
    return runCampaign(desc);
}

FaultResult QEMUSession::instructionSkipTest(const FaultDescriptor& desc) {
    return runCampaign(desc);
}

FaultResult QEMUSession::sensorCorruptionTest(const FaultDescriptor& desc) {
    return runCampaign(desc);
}

FaultResult QEMUSession::setPC(const FaultDescriptor& desc) {
    return runCampaign(desc);
}

// ============================================================================
//  Internal helpers
// ============================================================================

bool QEMUSession::bindServer() {
    serverSock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock_ < 0) {
        std::cerr << "[QEMUSession] socket() failed: " << strerror(errno) << "\n";
        return false;
    }

    int opt = 1;
    ::setsockopt(serverSock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(cfg_.serverPort);

    if (::bind(serverSock_,
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[QEMUSession] bind() failed: " << strerror(errno) << "\n";
        ::close(serverSock_);
        serverSock_ = -1;
        return false;
    }

    ::listen(serverSock_, 1);
    return true;
}

bool QEMUSession::launchQEMU() {
    std::ostringstream cmd;
    cmd << "qemu-system-arm"
        << " -machine "   << cfg_.machine
        << " -cpu "       << cfg_.cpu
        << " -kernel "    << cfg_.firmware
        << " -nographic"                       // routes UART to stdout → log
        << " -semihosting"                     // allows app to exit QEMU via bkpt 0xAB
        << " -plugin "    << cfg_.pluginPath
        << ",server=localhost:" << cfg_.serverPort
        << " > " << QEMU_LOG << " 2>&1";      // UART + plugin stderr → same log file

    std::cout << "\n[QEMUSession] ---- Launching QEMU ----------------------------\n";
    std::cout << "[QEMUSession] cmd: " << cmd.str() << "\n";

    qemuPid_ = ::fork();
    if (qemuPid_ < 0) {
        std::cerr << "[QEMUSession] fork() failed: " << strerror(errno) << "\n";
        return false;
    }
    if (qemuPid_ == 0) {
        ::execl("/bin/sh", "sh", "-c", cmd.str().c_str(), nullptr);
        ::_exit(127);
    }

    std::cout << "[QEMUSession] QEMU process started  PID=" << qemuPid_ << "\n";
    return true;
}

bool QEMUSession::acceptPlugin() {
    std::cout << "[QEMUSession] Waiting for plugin to connect"
              << " (timeout=" << cfg_.timeoutSecs << "s)...\n";

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(cfg_.timeoutSecs);

    while (std::chrono::steady_clock::now() < deadline) {
        sockaddr_in peer{};
        socklen_t   peerLen = sizeof(peer);
        pluginSock_ = ::accept(serverSock_,
                               reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (pluginSock_ >= 0) {
            char ipbuf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof(ipbuf));
            std::cout << "[QEMUSession] Plugin connected from "
                      << ipbuf << ":" << ntohs(peer.sin_port) << "\n";
            return true;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << "[QEMUSession] accept() error: " << strerror(errno) << "\n";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cerr << "[QEMUSession] ERROR: plugin did not connect within timeout\n";
    return false;
}

bool QEMUSession::sendDescriptor(const FaultDescriptor& desc) {
    std::cout << "\n[QEMUSession] ---- Sending fault descriptor ------------------\n";
    std::cout << "[QEMUSession]   fault_type     : " << faultTypeName(desc.fault_type) << "\n";
    std::cout << "[QEMUSession]   trigger        : " << triggerName(desc.trigger)      << "\n";
    std::cout << "[QEMUSession]   target_addr    : 0x" << std::hex << desc.target_addr    << std::dec << "\n";
    std::cout << "[QEMUSession]   inject_addr    : 0x" << std::hex << desc.inject_addr    << std::dec << "\n";
    std::cout << "[QEMUSession]   injected_value : 0x" << std::hex << (int)desc.injected_value << std::dec << "\n";
    std::cout << "[QEMUSession]   bit_pos        : "   << (int)desc.bit_pos        << "\n";
    std::cout << "[QEMUSession]   new_pc         : 0x" << std::hex << desc.new_pc  << std::dec << "\n";
    std::cout << "[QEMUSession]   sensor_addr    : 0x" << std::hex << desc.sensor_addr << std::dec << "\n";
    std::cout << "[QEMUSession]   target_count   : "   << desc.target_count   << "\n";
    std::cout << "[QEMUSession]   min_expected   : 0x" << std::hex << (int)desc.min_expected << std::dec << "\n";
    std::cout << "[QEMUSession]   max_expected   : 0x" << std::hex << (int)desc.max_expected << std::dec << "\n";
    std::cout << "[QEMUSession]   struct size    : "   << sizeof(desc) << " bytes\n";

    ssize_t sent = ::write(pluginSock_, &desc, sizeof(desc));
    if (sent != static_cast<ssize_t>(sizeof(desc))) {
        std::cerr << "[QEMUSession] ERROR: sendDescriptor write failed ("
                  << sent << "/" << sizeof(desc) << " bytes): "
                  << strerror(errno) << "\n";
        return false;
    }
    std::cout << "[QEMUSession] Descriptor written to socket (" << sent << " bytes)\n";

    std::cout << "[QEMUSession] Waiting for ACK from plugin...\n";
    uint8_t ack = 0;
    ssize_t n   = ::read(pluginSock_, &ack, 1);
    if (n != 1 || ack != ACK_READY) {
        std::cerr << "[QEMUSession] ERROR: bad ACK (got 0x"
                  << std::hex << (int)ack << std::dec
                  << ", expected 0xAC)\n";
        return false;
    }
    std::cout << "[QEMUSession] ACK received (0xAC) — plugin is ready\n";
    return true;
}

FaultResult QEMUSession::recvResult() {
    FaultResult result{};

    std::cout << "\n[QEMUSession] ---- Waiting for QEMU to exit ------------------\n";

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(cfg_.timeoutSecs);
    int status = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        pid_t r = ::waitpid(qemuPid_, &status, WNOHANG);
        if (r > 0) {
            std::cout << "[QEMUSession] QEMU exited  PID=" << r
                      << "  status=" << status << "\n";
            qemuPid_ = -1;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (qemuPid_ > 0) {
        std::cerr << "[QEMUSession] ERROR: QEMU timed out after "
                  << cfg_.timeoutSecs << "s — killing PID=" << qemuPid_ << "\n";
        ::kill(qemuPid_, SIGKILL);
        ::waitpid(qemuPid_, &status, 0);
        qemuPid_ = -1;
        printQemuLog();   // still print log so we can see what happened before timeout
        return result;
    }

    // Read FaultResult binary — plugin sends this just before closing the socket
    std::cout << "[QEMUSession] Reading FaultResult from plugin ("
              << sizeof(result) << " bytes)...\n";

    ssize_t n = ::read(pluginSock_, &result, sizeof(result));
    if (n != static_cast<ssize_t>(sizeof(result))) {
        std::cerr << "[QEMUSession] ERROR: recvResult short read ("
                  << n << "/" << sizeof(result) << " bytes)\n";
        printQemuLog();
        return result;
    }

    std::cout << "\n[QEMUSession] ---- Campaign result ---------------------------\n";
    std::cout << "[QEMUSession]   injected   : " << (result.injected ? "YES"    : "NO")     << "\n";
    std::cout << "[QEMUSession]   passed     : " << (result.passed   ? "PASSED" : "FAILED") << "\n";
    std::cout << "[QEMUSession]   insn_count : " << result.insn_count << "\n";
    std::cout << "[QEMUSession] ============================================\n\n";

    // Print log last — shows UART output + plugin key events in one block
    printQemuLog();

    return result;
}

// ── One-shot wrapper used by every fault method ───────────────────────────────
FaultResult QEMUSession::runCampaign(const FaultDescriptor& desc) {
    FaultResult failure{};

    auto cleanup = [&]() {
        if (qemuPid_ > 0) {
            std::cout << "[QEMUSession] Cleanup: killing QEMU PID=" << qemuPid_ << "\n";
            ::kill(qemuPid_, SIGTERM);
            int st; ::waitpid(qemuPid_, &st, 0);
            qemuPid_ = -1;
        }
        if (pluginSock_ >= 0) {
            ::close(pluginSock_);
            pluginSock_ = -1;
            std::cout << "[QEMUSession] Cleanup: plugin socket closed\n";
        }
    };

    if (!launchQEMU())         { cleanup(); return failure; }
    if (!acceptPlugin())       { cleanup(); return failure; }
    if (!sendDescriptor(desc)) { cleanup(); return failure; }

    std::cout << "[QEMUSession] Fault descriptor sent — QEMU is now executing...\n";

    FaultResult result = recvResult();
    cleanup();
    return result;
}