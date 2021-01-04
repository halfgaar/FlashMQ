#include "session.h"

Session::Session()
{

}

Session::Session(std::shared_ptr<Client> &client)
{
    this->client = client;
}

bool Session::clientDisconnected() const
{
    return client.expired();
}

std::shared_ptr<Client> Session::makeSharedClient() const
{
     return client.lock();
}
