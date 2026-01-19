#include "HardwareSession.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <regex>
#include <thread>
#include <fcntl.h>
#include <sys/select.h>
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
    std::string c = cmd + "\n";
    return write(sockfd, c.c_str(), c.size()) > 0;
}

std::string HardwareSession::recvResponse() {
    char buf[512];
    std::string out;
    
    // Set timeout for read operations
    timeval tv;
    tv.tv_sec = 2;  // 2 second timeout
    tv.tv_usec = 0;
    
    auto startTime = std::chrono::steady_clock::now();
    
    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        int ret = select(sockfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ret <= 0) break; // timeout or error
        
        ssize_t r = read(sockfd, buf, sizeof(buf) - 1);
        if (r <= 0) break;
        
        buf[r] = 0;
        out += buf;
        
        if (out.find(">") != std::string::npos) break;
        
        // Overall timeout check
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed > std::chrono::seconds(3)) break;
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
    
    return passed;
}
int HardwareSession::setPC(uint16_t pc) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "reg pc 0x%08X", pc);
    sendCmd("halt");
    recvResponse();
    sendCmd(cmd);
    recvResponse();
    sendCmd("resume");
    return 0;
}
