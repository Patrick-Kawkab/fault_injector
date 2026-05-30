#include"Session.h"
#include "HardwareSession.h"
#include "QemuSession.h"

#define ELF_FILE_PATH      "./tiva_c/tiva_led.elf" //temp fix


std::unique_ptr<Session> Session::create(const std::string& type){
    if(type == "qemu"){
        return std::make_unique<QEMUSession>(ELF_FILE_PATH);
    }
    else if(type=="hardware"){
        return std::make_unique<HardwareSession>();
    }
        throw std::invalid_argument("Invalid injector type: "+ type);
}