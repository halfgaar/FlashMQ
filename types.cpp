#include "types.h"

ConnAck::ConnAck(ConnAckReturnCodes return_code) :
    return_code(return_code)
{

}

SubAck::SubAck(uint16_t packet_id, const std::list<char> &subs_qos_reponses) :
    packet_id(packet_id)
{
    for (char ack_code : subs_qos_reponses)
    {
        responses.push_back(static_cast<SubAckReturnCodes>(ack_code));
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
