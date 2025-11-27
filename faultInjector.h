#ifndef FAULT_INJECTOR_H
#define FAULT_INJECTOR_H

#include <string>

class FaultInjector {
private:
    std::string name;      // injection type name
public:
    FaultInjector(std::string n) : name(n) {} ;

    virtual ~FaultInjector() = default;

    // All injectors must implement this
    virtual void inject() = 0;

};

#endif
