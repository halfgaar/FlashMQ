#include "session.h"

Session::Session()
{

}

bool Session::clientDisconnected() const
{
    return client.expired();
}

std::shared_ptr<Client> Session::makeSharedClient() const
{
    return client.lock();
}

void Session::assignActiveConnection(std::shared_ptr<Client> &client)
{
    this->client = client;
}
