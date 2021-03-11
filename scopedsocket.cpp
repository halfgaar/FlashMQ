#include "scopedsocket.h"

ScopedSocket::ScopedSocket(int socket) : socket(socket)
{

}

ScopedSocket::ScopedSocket(ScopedSocket &&other)
{
    this->socket = other.socket;
    other.socket = 0;
}

ScopedSocket::~ScopedSocket()
{
    if (socket > 0)
        close(socket);
}
