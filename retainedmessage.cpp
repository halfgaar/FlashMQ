#include "retainedmessage.h"

RetainedMessage::RetainedMessage(const std::string &topic, const std::string &payload, char qos) :
    topic(topic),
    payload(payload),
    qos(qos)
{

}

bool RetainedMessage::operator==(const RetainedMessage &rhs) const
{
    return this->topic == rhs.topic;
}
