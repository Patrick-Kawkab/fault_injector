#include <iostream>
#include <string>
#include <unistd.h>
#include <signal.h>
#include <fstream>
#include <sys/wait.h>
#include "QemuSession.h"

QemuSession::QemuSession(const std::string &firmwarePath, const std::string &arch, const std::string &micro, const std::string &gdb_ip , int gdb_port )
{
    this->gdb_ip = gdb_ip;
    this->gdb_port = gdb_port;
    if (arch == "avr")
    {
        cmd = "qemu-system-" + arch + " -machine " + micro +
              " -bios " + firmwarePath +
              " -nographic -S -gdb tcp:" + std::to_string(gdb_port);
    }
    else if (arch == "arm")
    {
        cmd = "qemu-system-" + arch + " -machine " + micro +
              " -kernel " + firmwarePath +
              " -nographic -S -gdb tcp:" + std::to_string(gdb_port);
    }
    else if (arch == "tricore")
    {
        cmd = "qemu-system-" + arch + " -cpu " + micro +
              " -kernel " + firmwarePath +
              " -nographic -S -gdb tcp:" + std::to_string(gdb_port);
    }
}
int QemuSession::start()
{
    pid = fork();
    if (pid == 0)
    {
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)nullptr);
        _exit(1);
    }
    else if (pid > 0)
    {
        std::cout << "QEMU started with PID: " << pid << std::endl;
        // Connect to GDB stub
        if (!gdb.connectToGDB(gdb_ip, gdb_port))
        {
            std::cerr << "Failed to connect to GDB stub" << std::endl;
        }
        else
        {
            std::cout << "Connected to GDB stub at " << gdb_ip << ":" << gdb_port << std::endl;
        }
        return 0;
    }
    else
    {
        std::cerr << "Failed to fork()" << std::endl;
        return -1;
    }
}
int QemuSession::stop() noexcept
{
    if (pid <= 0)
    {
        return -1;
    }

    std::cout << "killing QEMU with pid :" << pid << std::endl;

    kill(pid, SIGTERM);

    // Wait for the child process to terminate gracefully
    int status;
    waitpid(pid, &status, 0);

    std::cout << "QEMU process " << pid << " terminated." << std::endl;

    // Reset pid to indicate the session is no longer running
    pid = -1;

    return 0;
}
int QemuSession::setPC(uint16_t pc)
{
    std::cout << "jump instruction to jump to address: " << pc << std::endl;
    return 0;
}
int QemuSession::flipRegisterBit(std::string reg, int bit)
{
    std::cout << "flipping bit no." << bit << "in reg :" << reg << std::endl;
    return 0;
}
int QemuSession::writeMemory(uint16_t addr, uint8_t value)
{
    std::cout << "writing in addr: " << addr << "\n value: " << value << std::endl;
    return 0;
}

QemuSession::~QemuSession()
{
    stop();
}
