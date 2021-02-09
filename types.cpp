#include "types.h"

ConnAck::ConnAck(ConnAckReturnCodes return_code, bool session_present) :
    return_code(return_code),
    session_present(session_present)
{
    // [MQTT-3.2.2-4]
    if (return_code > ConnAckReturnCodes::Accepted)
        session_present = false;
}

SubAck::SubAck(uint16_t packet_id, const std::list<char> &subs_qos_reponses) :
    packet_id(packet_id)
{
    for (char ack_code : subs_qos_reponses)
    {
        responses.push_back(static_cast<SubAckReturnCodes>(ack_code));
    }
}

size_t SubAck::getLengthWithoutFixedHeader() const
{
    size_t result = responses.size();
    result += 2; // Packet ID
    return result;
}

Publish::Publish(const std::string &topic, const std::string payload, char qos) :
    topic(topic),
    payload(payload),
    qos(qos)
{

}

size_t Publish::getLengthWithoutFixedHeader() const
{
    int result = topic.length() + payload.length() + 2;

    if (qos)
        result += 2;

    return result;
}

PubAck::PubAck(uint16_t packet_id) :
    packet_id(packet_id)
{

}

// Packet has no payload and only a variable header, of length 2.
size_t PubAck::getLengthWithoutFixedHeader() const
{
    return 2;
}
