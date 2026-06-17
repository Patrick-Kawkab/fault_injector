#include "HardwareSession.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <regex>
#include <thread>
#include <fcntl.h>
#include <sys/select.h>
#include <iomanip>
HardwareSession::HardwareSession(const std::string& h, int p)
    : sockfd(-1), host(h), port(p) {}

HardwareSession::~HardwareSession() {
    stop();
}
int HardwareSession::start() {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }

    recvResponse(); 
    sendCmd("halt");
    recvResponse(); 
    return 0;
}

int HardwareSession::stop() noexcept {
    if (sockfd >= 0) {
        sendCmd("exit");
        close(sockfd);
        sockfd = -1;
    }
    return 0;
}
bool HardwareSession::sendCmd(const std::string& cmd) {
    if (sockfd < 0) return false;

    std::string c = cmd + "\n";
    size_t total = 0;

    while (total < c.size()) {
        ssize_t r = send(sockfd, c.c_str() + total, c.size() - total, MSG_NOSIGNAL);
        if (r < 0) return false;
        total += r;
    }
    return true;
}

std::string HardwareSession::recvResponse() {
    char buf[512];
    std::string out;
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        // reset tv inside the loop — select() consumes it on Linux
        timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        int ret = select(sockfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ret < 0) break;   // error
        if (ret == 0) break;  // per-read timeout

        ssize_t r = read(sockfd, buf, sizeof(buf) - 1);
        if (r < 0) break;     // error
        if (r == 0) break;    // connection closed by peer

        buf[r] = '\0';
        out += buf;

        if (out.find('>') != std::string::npos) break;

        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed > std::chrono::seconds(5)) break;
    }

    return out;
}
FaultResult HardwareSession::memoryCorruptionTest(const FaultDescriptor& desc)
{
    uint32_t addr          = desc.inject_addr;
    uint8_t  injectedValue = desc.injected_value;
    uint8_t  minExpected   = desc.min_expected;
    uint8_t  maxExpected   = desc.max_expected;
    uint8_t  delay_ms      = static_cast<uint8_t>(desc.target_count);

    char cmd[128];

    FaultResult result{};
    result.insn_count = desc.target_count;

    // Halt
    sendCmd("halt");
    recvResponse();

    // Write corrupted value
    snprintf(cmd, sizeof(cmd), "mwb 0x%08X 0x%02X", addr, injectedValue);
    sendCmd(cmd);
    recvResponse();  // Consume the mwb response

    // Resume
    sendCmd("resume");
    recvResponse();  // Consume the resume response

    // Wait for firmware to process/recover
    std::this_thread::sleep_for(std::chrono::milliseconds(10+delay_ms));

    // Halt again
    sendCmd("halt");
    recvResponse();  // Consume the halt response

    // Read the value
    snprintf(cmd, sizeof(cmd), "mdb 0x%08X", addr);
    sendCmd(cmd);
    std::string resp = recvResponse();  // NOW read the mdb response

    // Parse
    std::regex r("0x[0-9a-fA-F]+[:\\s]+([0-9a-fA-F]{2})");
    std::smatch m;

    if (!std::regex_search(resp, m, r)) {
        std::cerr << "Failed to parse response: " << resp << std::endl;
        result.injected = 1;
        result.passed = 0;
        return result;
    }

    uint8_t readVal = static_cast<uint8_t>(std::stoul(m[1], nullptr, 16));


    bool passed = (readVal >= minExpected && readVal <= maxExpected);

    sendCmd("resume");

    result.injected = 1;
    result.passed = passed ? 1 : 0;
    return result;
}
FaultResult HardwareSession::sensorCorruptionTest(const FaultDescriptor& desc)
{
    uint32_t encoderCountAddr = desc.inject_addr;
    uint32_t cruiseStateAddr  = desc.sensor_addr;
    uint32_t durationMs       = static_cast<uint32_t>(desc.duration_Ms);
    uint32_t intervalMs       = static_cast<uint32_t>(desc.observe_window); 

    char cmd[128];

    FaultResult result{};
    result.insn_count = desc.target_count;

    // ── PRE-CHECK: cruise must be STATE_ACTIVE ──────────────────────
    sendCmd("halt");
    recvResponse();

    snprintf(cmd, sizeof(cmd), "mdw 0x%08X", cruiseStateAddr);
    sendCmd(cmd);
    std::string preResp = recvResponse();

    sendCmd("resume");
    recvResponse();

    std::regex wordReg("0x[0-9a-fA-F]+[:\\s]+([0-9a-fA-F]{8})");
    std::smatch m;
    if (!std::regex_search(preResp, m, wordReg)) {
        std::cerr << "[sensorCorruptionTest] Failed to parse cruise_state: "
                  << preResp << "\n";
        result.injected = 0;
        result.passed = 0;
        return result;
    }

    uint32_t cruiseStatePre = std::stoul(m[1], nullptr, 16);
    if (cruiseStatePre != 1) {   // STATE_ACTIVE == 1
        std::cerr << "[sensorCorruptionTest] Cruise not active (cruise_state="
                  << cruiseStatePre << "), aborting.\n";
        result.injected = 0;
        result.passed = 0;
        return result;
    }

    // ── INJECTION LOOP ──────────────────────────────────────────────
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(durationMs);

    while (std::chrono::steady_clock::now() < deadline) {
        sendCmd("halt");
        recvResponse();

        // encoder_count is uint32_t → use mww
        snprintf(cmd, sizeof(cmd), "mww 0x%08X 0x00000000", encoderCountAddr);
        sendCmd(cmd);
        recvResponse();

        sendCmd("resume");
        recvResponse();

        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }

    // ── POST-CHECK: cruise must now be STATE_OFF ────────────────────
    sendCmd("halt");
    recvResponse();

    snprintf(cmd, sizeof(cmd), "mdw 0x%08X", cruiseStateAddr);
    sendCmd(cmd);
    std::string postResp = recvResponse();

    sendCmd("resume");
    recvResponse();

    if (!std::regex_search(postResp, m, wordReg)) {
        std::cerr << "[sensorCorruptionTest] Failed to parse post cruise_state: "
                  << postResp << "\n";
        result.injected = 1;
        result.passed = 0;
        return result;
    }

    uint32_t cruiseStatePost = std::stoul(m[1], nullptr, 16);
    bool passed = (cruiseStatePost == 0);   // STATE_OFF == 0

    if (!passed)
        std::cerr << "[sensorCorruptionTest] Cruise still active after injection "
                     "(cruise_state=" << cruiseStatePost << ")\n";

    result.injected = 1;
    result.passed = passed ? 1 : 0;
    return result;
}
FaultResult HardwareSession::Task_delay(const FaultDescriptor& desc)
{
    uint32_t samplePeriodAddr = desc.inject_addr;
    uint32_t cruiseStateAddr  = desc.sensor_addr;
    uint32_t corruptedValue   = desc.target_addr;
    uint32_t durationMs       = static_cast<uint32_t>(desc.duration_Ms);
    uint32_t intervalMs       = static_cast<uint32_t>(desc.observe_window);

    char cmd[128];
    std::regex wordReg("0x[0-9a-fA-F]+[:\\s]+([0-9a-fA-F]{8})");
    std::smatch m;

    FaultResult result{};
    result.insn_count = desc.target_count;

    // ── PRE-CHECK: cruise must be STATE_ACTIVE ──────────────────────
    sendCmd("halt");
    recvResponse();

    snprintf(cmd, sizeof(cmd), "mdw 0x%08X", cruiseStateAddr);
    sendCmd(cmd);
    std::string preResp = recvResponse();

    sendCmd("resume");
    recvResponse();

    if (!std::regex_search(preResp, m, wordReg)) {
        std::cerr << "[taskDelayTest] Failed to parse cruise_state: "
                  << preResp << "\n";
        result.injected = 0;
        result.passed = 0;
        return result;
    }

    uint32_t cruiseStatePre = std::stoul(m[1], nullptr, 16);
    if (cruiseStatePre != 1) {
        std::cerr << "[taskDelayTest] Cruise not active (cruise_state="
                  << cruiseStatePre << "), aborting.\n";
        result.injected = 0;
        result.passed = 0;
        return result;
    }

    // ── INJECTION LOOP ──────────────────────────────────────────────
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(durationMs);

    while (std::chrono::steady_clock::now() < deadline) {
        sendCmd("halt");
        recvResponse();

        // encoder_sample_period is uint32_t → mww
        snprintf(cmd, sizeof(cmd), "mww 0x%08X 0x%08X",
                 samplePeriodAddr, corruptedValue);
        sendCmd(cmd);
        recvResponse();

        sendCmd("resume");
        recvResponse();

        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }

    // ── POST-CHECK: cruise must now be STATE_OFF ────────────────────
    sendCmd("halt");
    recvResponse();

    snprintf(cmd, sizeof(cmd), "mdw 0x%08X", cruiseStateAddr);
    sendCmd(cmd);
    std::string postResp = recvResponse();

    // ── RESTORE: write original value back ─────────────────────────
    snprintf(cmd, sizeof(cmd), "mww 0x%08X 0x%08X",
             samplePeriodAddr, 100);          // SAMPLE_PERIOD_MS = 100
    sendCmd(cmd);
    recvResponse();

    sendCmd("resume");
    recvResponse();

    if (!std::regex_search(postResp, m, wordReg)) {
        std::cerr << "[taskDelayTest] Failed to parse post cruise_state: "
                  << postResp << "\n";
        result.injected = 1;
        result.passed = 0;
        return result;
    }

    uint32_t cruiseStatePost = std::stoul(m[1], nullptr, 16);
    bool passed = (cruiseStatePost == 0);   // STATE_OFF == 0

    if (!passed)
        std::cerr << "[taskDelayTest] Cruise still active after delay injection "
                     "(cruise_state=" << cruiseStatePost << ")\n";

    result.injected = 1;
    result.passed = passed ? 1 : 0;
    return result;
}
FaultResult HardwareSession::bitFlipTest(const FaultDescriptor& desc)
{
    uint32_t addr         = desc.inject_addr;
    uint8_t  bit_position = desc.bit_pos;
    uint8_t  minExpected  = desc.min_expected;
    uint8_t  maxExpected  = desc.max_expected;
    uint8_t  delay_ms     = static_cast<uint8_t>(desc.target_count);

    char cmd[128];

    FaultResult result{};
    result.insn_count = desc.target_count;

    // ── HALT ────────────────────────────────────────────────────────
    sendCmd("halt");
    recvResponse();

    // ── READ CURRENT VALUE ──────────────────────────────────────────
    snprintf(cmd, sizeof(cmd), "mdb 0x%08X", addr);
    sendCmd(cmd);
    std::string readResp = recvResponse();

    std::regex r("0x[0-9a-fA-F]+[:\\s]+([0-9a-fA-F]{2})");
    std::smatch m;

    if (!std::regex_search(readResp, m, r)) {
        std::cerr << "[bitFlip] Failed to parse current value: "
                  << readResp << "\n";
        sendCmd("resume");
        recvResponse();
        result.injected = 0;
        result.passed = 0;
        return result;
    }

    uint8_t currentVal = static_cast<uint8_t>(std::stoul(m[1], nullptr, 16));
    std::cout << "[bitFlip] Current value: 0x" << std::hex
              << (int)currentVal << std::dec << "\n";

    // ── FLIP THE BIT ────────────────────────────────────────────────
    uint8_t flippedVal = currentVal ^ (1 << bit_position);
    std::cout << "[bitFlip] Flipped value: 0x" << std::hex
              << (int)flippedVal << std::dec
              << " (bit " << (int)bit_position << " flipped)\n";

    // ── WRITE FLIPPED VALUE ─────────────────────────────────────────
    snprintf(cmd, sizeof(cmd), "mwb 0x%08X 0x%02X", addr, flippedVal);
    sendCmd(cmd);
    recvResponse();

    // ── RESUME ──────────────────────────────────────────────────────
    sendCmd("resume");
    recvResponse();

    // ── WAIT FOR FIRMWARE TO REACT ──────────────────────────────────
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

    // ── HALT AGAIN ──────────────────────────────────────────────────
    sendCmd("halt");
    recvResponse();

    // ── READ BACK ───────────────────────────────────────────────────
    snprintf(cmd, sizeof(cmd), "mdb 0x%08X", addr);
    sendCmd(cmd);
    std::string postResp = recvResponse();

    sendCmd("resume");
    recvResponse();

    if (!std::regex_search(postResp, m, r)) {
        std::cerr << "[bitFlip] Failed to parse post value: "
                  << postResp << "\n";
        result.injected = 1;
        result.passed = 0;
        return result;
    }

    uint8_t readVal = static_cast<uint8_t>(std::stoul(m[1], nullptr, 16));
    std::cout << "[bitFlip] Read back value: 0x" << std::hex
              << (int)readVal << std::dec << "\n";

    // ── EVALUATE ────────────────────────────────────────────────────
    bool passed = (readVal >= minExpected && readVal <= maxExpected);

    if (!passed)
        std::cerr << "[bitFlip] Value 0x" << std::hex << (int)readVal
                  << " out of range [0x" << (int)minExpected
                  << ", 0x" << (int)maxExpected << "]\n";

    result.injected = 1;
    result.passed = passed ? 1 : 0;
    return result;
}
FaultResult HardwareSession::setPC(const FaultDescriptor& desc)
{
    uint32_t badPC          = desc.new_pc;
    uint32_t cruiseStateAddr = desc.sensor_addr;
    uint32_t wait_ms        = static_cast<uint32_t>(desc.target_count);

    char cmd[128];
    std::regex wordReg("0x[0-9a-fA-F]+[:\\s]+([0-9a-fA-F]{8})");
    std::smatch m;

    FaultResult result{};
    result.insn_count = desc.target_count;

    // ── PRE-CHECK: cruise must be STATE_ACTIVE ──────────────────────
    sendCmd("halt");
    recvResponse();

    snprintf(cmd, sizeof(cmd), "mdw 0x%08X", cruiseStateAddr);
    sendCmd(cmd);
    std::string preResp = recvResponse();

    if (!std::regex_search(preResp, m, wordReg)) {
        std::cerr << "[pcCorruptionTest] Failed to parse cruise_state: "
                  << preResp << "\n";
        sendCmd("resume");
        recvResponse();
        result.injected = 0;
        result.passed = 0;
        return result;
    }

    uint32_t cruiseStatePre = std::stoul(m[1], nullptr, 16);
    if (cruiseStatePre != 1) {
        std::cerr << "[pcCorruptionTest] Cruise not active (cruise_state="
                  << cruiseStatePre << "), aborting.\n";
        sendCmd("resume");
        recvResponse();
        result.injected = 0;
        result.passed = 0;
        return result;
    }

    // ── CORRUPT PC ──────────────────────────────────────────────────
    snprintf(cmd, sizeof(cmd), "reg pc 0x%08X", badPC);
    sendCmd(cmd);
    recvResponse();

    sendCmd("resume");
    recvResponse();

    std::cout << "[pcCorruptionTest] PC set to 0x" << std::hex << badPC
               << std::dec << ", resumed. Waiting " << wait_ms << "ms...\n";

    // ── WAIT past the expected watchdog timeout ─────────────────────
    std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));

    // ── POST-CHECK: try to halt and read cruise_state ───────────────
    sendCmd("halt");
    std::string haltResp = recvResponse();

    if (haltResp.empty()) {
        std::cerr << "[pcCorruptionTest] No response from target after halt — "
                     "system may be unresponsive.\n";
        result.injected = 1;
        result.passed = 0;
        return result;
    }

    snprintf(cmd, sizeof(cmd), "mdw 0x%08X", cruiseStateAddr);
    sendCmd(cmd);
    std::string postResp = recvResponse();

    sendCmd("resume");
    recvResponse();

    if (!std::regex_search(postResp, m, wordReg)) {
        std::cerr << "[pcCorruptionTest] Failed to parse post cruise_state: "
                  << postResp << "\n";
        result.injected = 1;
        result.passed = 0;
        return result;
    }

    uint32_t cruiseStatePost = std::stoul(m[1], nullptr, 16);
    bool passed = (cruiseStatePost == 0);   // STATE_OFF == 0, implies reset occurred

    if (!passed)
        std::cerr << "[pcCorruptionTest] System did not recover to safe state "
                     "(cruise_state=" << cruiseStatePost << ")\n";

    result.injected = 1;
    result.passed = passed ? 1 : 0;
    return result;
}
