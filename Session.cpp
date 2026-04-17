#include"Session.h"
#include "HardwareSession.h"
#include "QemuSession.h"

std::unique_ptr<Session> Session::create(const std::string& type){
    if(type == "qemu"){
        return std::make_unique<QemuSession>();
    }
    else if(type=="hardware"){
        return std::make_unique<HardwareSession>();
    }
        throw std::invalid_argument("Invalid injector type: "+ type);
}