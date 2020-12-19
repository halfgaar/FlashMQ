#include "types.h"

ConnAck::ConnAck(ConnAckReturnCodes return_code) :
    return_code(return_code)
{

}

SubAck::SubAck(uint16_t packet_id, const std::list<std::string> &subs) :
    packet_id(packet_id)
{
    // dummy
    for(size_t i = 0; i < subs.size(); i++)
    {
        responses.push_back(SubAckReturnCodes::MaxQoS0);
    }
}

Publish::Publish(const std::string &topic, const std::string payload, char qos) :
    topic(topic),
    payload(payload),
    qos(qos)
{

}

// Length starting at the variable header, not the fixed header.
size_t Publish::getLength() const
{
    int result = topic.length() + payload.length() + 2;

    if (qos)
        result += 2;

    return result;
}
