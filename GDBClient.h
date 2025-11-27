#ifndef GDBCLIENT_H
#define GDBCLIENT_H
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include "GDBClient.h"
class GDBClient
{
private:
    int sock = -1;

public:
    bool connectToGDB(const std::string &ip, int port);

    void sendPacket(const std::string &data);

    std::string checksum(const std::string &data);

    ~GDBClient();
};

#endif