#ifndef SESSION_H
#define SESSION_H

#include <memory>

class Client;

class Session
{
    std::weak_ptr<Client> client;
    // TODO: qos message queue, as some kind of movable pointer.
public:
    Session();
    Session(std::shared_ptr<Client> &client);

    bool clientDisconnected() const;
    std::shared_ptr<Client> makeSharedClient() const;
};

#endif // SESSION_H
