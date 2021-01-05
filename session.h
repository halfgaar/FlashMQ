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
    Session(const Session &other) = delete;
    Session(Session &&other) = delete;

    bool clientDisconnected() const;
    std::shared_ptr<Client> makeSharedClient() const;
    void assignActiveConnection(std::shared_ptr<Client> &client);
};

#endif // SESSION_H
