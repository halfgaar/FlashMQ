#ifndef FORWARD_DECLARATIONS_H
#define FORWARD_DECLARATIONS_H

#include <memory>

class Client;
typedef std::shared_ptr<Client> Client_p;
class ThreadData;
typedef std::shared_ptr<ThreadData> ThreadData_p;
class MqttPacket;
class SubscriptionStore;
class Session;
class Settings;


#endif // FORWARD_DECLARATIONS_H
