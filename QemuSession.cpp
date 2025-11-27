#include<iostream>
#include <string>
#include <unistd.h>
#include <signal.h>



class QemuSession{
    private:
        std::string cmd;
        pid_t pid=-1;
    public:
        QemuSession(const std::string& firmwarePath,const std::string arch,const std::string micro){
            if(arch == "avr"){
                cmd ="qemu-system-"+arch+" -machine "+micro+" -bios "+firmwarePath+" -nographic";
            }
            else if(arch=="arm"){
                cmd ="qemu-system-"+arch+" -machine "+micro+" -kernel "+firmwarePath+" -nographic";
            }
            else if(arch=="tricore"){
                cmd ="qemu-system-"+arch+" -cpu "+micro+" -kernel "+firmwarePath+" -nographic";
            }

        }
        int start(){
            pid = fork();
            if(pid ==0){
                execl("/bin/sh", "sh","-c",cmd.c_str(),(char*)nullptr);
                _exit(1);
            }
            else if(pid>0){
                std::cout<<"QEMU started with PID: "<<pid<<std::endl;
                return 0;
            }
            else{
                std::cerr<<"failed to fork()"<<std::endl;
                return -1;
            }
        }
	int stop(){
        if(pid<=0){
            std::cerr<<"QEMU is not runnning"<<std::endl;
            return -1;
        }
        std::cout<<"killing QEMU with pid :"<<pid<<std::endl;
        kill(pid,SIGTERM);
        return 0;
	}
};
