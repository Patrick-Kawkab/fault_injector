#ifndef QEMUSESSION_H
#define QEMUSESSION_h
#include "GDBClient.h"
class QemuSession
{
private:
    std::string cmd;
    pid_t pid = -1;
    GDBClient gdb;
    std::string gdb_ip;
    int gdb_port;

public:
    QemuSession(const std::string &firmwarePath, const std::string &arch, const std::string &micro, const std::string &gdb_ip = "127.0.0.1", int gdb_port = 1234);
    int start();
    int stop() noexcept;
    int setPC(uint16_t pc);
    int flipRegisterBit(std::string reg, int bit);
    int writeMemory(uint16_t addr, uint8_t value);
    ~QemuSession();
};
#endif