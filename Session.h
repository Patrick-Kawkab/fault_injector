#ifndef SESSION_H
#define SESSION_H

#include <iostream>
#include <string>
#include <fstream>
#include <memory>
#include <cstdint>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "FaultConfig.h"

class Session {
public:
    virtual ~Session()= default;

    virtual int start()=0;
    virtual int stop() noexcept=0;

    virtual FaultResult setPC(const FaultDescriptor& desc)=0;
    virtual FaultResult memoryCorruptionTest(const FaultDescriptor& desc)=0;
    virtual FaultResult bitFlipTest(const FaultDescriptor& desc)=0;    
    virtual FaultResult instructionSkipTest(const FaultDescriptor& desc)=0;
    virtual FaultResult sensorCorruptionTest(const FaultDescriptor& desc)=0;
    
    static std::unique_ptr<Session> create(const std::string& type);
};
    
#endif