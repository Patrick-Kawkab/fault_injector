#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include "GDBClient.h"

bool GDBClient::connectToGDB(const std::string &ip, int port)
{
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        return false;
    }
    return true;
}

void GDBClient::sendPacket(const std::string &data)
{
    if (sock >= 0)
    {
        std::string packet = "$" + data + "#" + checksum(data);
        write(sock, packet.c_str(), packet.size());
    }
}

std::string GDBClient::checksum(const std::string &data)
{
    int csum = 0;
    for (char ch : data)
        csum += (unsigned char)ch;
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", csum & 0xFF);
    return std::string(buf);
}

GDBClient::~GDBClient()
{
    if (sock >= 0)
        close(sock);
}
