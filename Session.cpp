#include"Session.h"
#include "HardwareSession.h"
#include "QemuSession.h"
#include<memory>
#include<stdexcept>


std::unique_ptr<Session> Session::create(const std::string& type ,  const QemuSessionConfig* qemuCfg = nullptr){
    if(type == "qemu")
    {
        if(qemuCfg == nullptr)
            throw std::runtime_error("QEMU config is required");

        return std::make_unique<QEMUSession>(*qemuCfg);
    }

    if(type == "hardware")
    {
        return std::make_unique<HardwareSession>();
    }

        throw std::invalid_argument("Invalid injector type: "+ type);
}