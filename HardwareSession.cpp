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
bool HardwareSession::memoryCorruptionTest(uint32_t addr, uint8_t injectedValue,
                                           uint8_t minExpected, uint8_t maxExpected)
{
    char cmd[128];
    
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
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
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
        return false;
    }
    
    uint8_t readVal = static_cast<uint8_t>(std::stoul(m[1], nullptr, 16));
    
    
    bool passed = (readVal >= minExpected && readVal <= maxExpected);
        
    sendCmd("resume");
    return passed;
}
int HardwareSession::setPC(uint32_t pc) {          // uint32_t, not uint16_t
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "reg pc 0x%08X", pc);

    if (!sendCmd("halt"))     return -1;
    recvResponse();

    if (!sendCmd(cmd))        return -1;
    recvResponse();

    if (!sendCmd("resume"))   return -1;
    recvResponse();            // consume resume response

    return 0;
}
bool HardwareSession::memoryCorruptionTest(uint32_t addr,
                                           uint8_t  injectedValue,
                                           uint8_t  minExpected,
                                           uint8_t  maxExpected)
{
    char cmd[64];
    bool testPassed = false;

    // --- 1. Halt ---
    if (!sendCmd("halt")) {
        std::cerr << "[memoryCorruptionTest] Failed to send halt\n";
        return false;
    }
    recvResponse();

    // --- 2. Inject value ---
    snprintf(cmd, sizeof(cmd), "mwb 0x%08X 0x%02X", addr, injectedValue);
    if (!sendCmd(cmd)) {
        std::cerr << "[memoryCorruptionTest] Failed to send mwb\n";
        sendCmd("resume");
        recvResponse();
        return false;
    }
    recvResponse();

    // --- 3. Resume and let firmware process/react ---
    if (!sendCmd("resume")) {
        std::cerr << "[memoryCorruptionTest] Failed to send resume\n";
        return false;
    }
    recvResponse();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // --- 4. Halt again ---
    if (!sendCmd("halt")) {
        std::cerr << "[memoryCorruptionTest] Failed to send second halt\n";
        return false;
    }
    recvResponse();

    // --- 5. Read back ---
    snprintf(cmd, sizeof(cmd), "mdb 0x%08X", addr);
    if (!sendCmd(cmd)) {
        std::cerr << "[memoryCorruptionTest] Failed to send mdb\n";
        sendCmd("resume");
        recvResponse();
        return false;
    }

    std::string resp = recvResponse();

    // --- 6. Parse OpenOCD response ---
    // OpenOCD mdb format: "0x20001000: ff \n"
    std::regex pattern(R"(0x[0-9a-fA-F]+:\s+([0-9a-fA-F]{2}))");
    std::smatch match;

    if (!std::regex_search(resp, match, pattern)) {
        std::cerr << "[memoryCorruptionTest] Failed to parse mdb response: '"
                  << resp << "'\n";
        sendCmd("resume");
        recvResponse();
        return false;
    }

    uint8_t readVal = static_cast<uint8_t>(std::stoul(match[1], nullptr, 16));

    // --- 7. Evaluate ---
    testPassed = (readVal >= minExpected && readVal <= maxExpected);

    if (!testPassed) {
        std::cerr << "[memoryCorruptionTest] FAIL: "
                  << "addr=0x" << std::hex << std::setw(8) << std::setfill('0') << addr
                  << " injected=0x"  << std::setw(2) << (int)injectedValue
                  << " readback=0x"  << std::setw(2) << (int)readVal
                  << " expected=[0x" << std::setw(2) << (int)minExpected
                  << "..0x"          << std::setw(2) << (int)maxExpected << "]"
                  << std::dec << "\n";
    }

    // --- 8. Always resume (even on failure) ---
    if (!sendCmd("resume")) {
        std::cerr << "[memoryCorruptionTest] Failed to send final resume\n";
    }
    recvResponse();

    return testPassed;
}