/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

#include "cassert"

#include "types.h"
#include "mqtt5properties.h"

ConnAck::ConnAck(const ProtocolVersion protVersion, ReasonCodes return_code, bool session_present) :
    protocol_version(protVersion),
    session_present(session_present)
{

    if (this->protocol_version <= ProtocolVersion::Mqtt311)
    {
        ConnAckReturnCodes mqtt3_return = ConnAckReturnCodes::Accepted;

        switch (return_code)
        {
        case ReasonCodes::Success:
            mqtt3_return = ConnAckReturnCodes::Accepted;
            break;
        case ReasonCodes::UnsupportedProtocolVersion:
            mqtt3_return = ConnAckReturnCodes::UnacceptableProtocolVersion;
            break;
        case ReasonCodes::ClientIdentifierNotValid:
            mqtt3_return = ConnAckReturnCodes::ClientIdRejected;
            break;
        case ReasonCodes::ServerUnavailable:
            mqtt3_return = ConnAckReturnCodes::ServerUnavailable;
            break;
        case ReasonCodes::BadUserNameOrPassword:
            mqtt3_return = ConnAckReturnCodes::MalformedUsernameOrPassword;
            break;
        case ReasonCodes::NotAuthorized:
            mqtt3_return = ConnAckReturnCodes::NotAuthorized;
            break;
        default:
            assert(false);
        }

        // [MQTT-3.2.2-4]
        if (mqtt3_return > ConnAckReturnCodes::Accepted)
            session_present = false;

        this->return_code = static_cast<uint8_t>(mqtt3_return);
    }
    else
    {
        this->return_code = static_cast<uint8_t>(return_code);

        // MQTT-3.2.2-6
        if (this->return_code > 0)
            session_present = false;
    }
}

size_t ConnAck::getLengthWithoutFixedHeader() const
{
    size_t result = 2;

    if (this->protocol_version >= ProtocolVersion::Mqtt5)
    {
        const size_t proplen = propertyBuilder ? propertyBuilder->getLength() : 1;
        result += proplen;
    }
    return result;
}

SubAck::SubAck(uint16_t packet_id, const std::list<char> &subs_qos_reponses) :
    packet_id(packet_id)
{
    assert(!subs_qos_reponses.empty());

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

Publish::Publish(const std::string &topic, const std::string &payload, char qos) :
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

void Publish::setClientSpecificProperties()
{
    propertyBuilder->clearClientSpecificBytes();
    // TODO. Expires at?
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

UnsubAck::UnsubAck(uint16_t packet_id) :
    packet_id(packet_id)
{

}

size_t UnsubAck::getLengthWithoutFixedHeader() const
{
    return 2;
}

PubRec::PubRec(uint16_t packet_id) :
    packet_id(packet_id)
{

}

size_t PubRec::getLengthWithoutFixedHeader() const
{
    return 2;
}

PubComp::PubComp(uint16_t packet_id) :
    packet_id(packet_id)
{

}

size_t PubComp::getLengthWithoutFixedHeader() const
{
    return 2;
}

PubRel::PubRel(uint16_t packet_id) :
    packet_id(packet_id)
{

}

size_t PubRel::getLengthWithoutFixedHeader() const
{
    return 2;
}
