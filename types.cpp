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
#include "mqttpacket.h"

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

SubAck::SubAck(const ProtocolVersion protVersion, uint16_t packet_id, const std::list<char> &subs_qos_reponses) :
    protocol_version(protVersion),
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

    if (this->protocol_version >= ProtocolVersion::Mqtt5)
    {
        const size_t proplen = propertyBuilder ? propertyBuilder->getLength() : 1;
        result += proplen;
    }
    return result;
}

PublishBase::PublishBase(const std::string &topic, const std::string &payload, char qos) :
    topic(topic),
    payload(payload),
    qos(qos)
{

}

size_t PublishBase::getLengthWithoutFixedHeader() const
{
    const int topicLength = this->skipTopic ? 0 : topic.length();
    int result = topicLength + payload.length() + 2;

    if (qos)
        result += 2;

    return result;
}

/**
 * @brief Publish::setClientSpecificProperties generates the properties byte array for one client. You're supposed to call it before any publish.
 *
 */
void PublishBase::setClientSpecificProperties()
{
    if (this->createdAt.time_since_epoch().count() && this->topicAlias == 0)
        return;

    if (propertyBuilder)
        propertyBuilder->clearClientSpecificBytes();
    else
        propertyBuilder = std::make_shared<Mqtt5PropertyBuilder>();

    if (createdAt.time_since_epoch().count() > 0)
    {
        auto now = std::chrono::steady_clock::now();
        std::chrono::seconds newExpiresAfter = std::chrono::duration_cast<std::chrono::seconds>(now - createdAt);
        propertyBuilder->writeMessageExpiryInterval(newExpiresAfter.count());
    }

    if (topicAlias > 0)
        propertyBuilder->writeTopicAlias(this->topicAlias);
}

void PublishBase::constructPropertyBuilder()
{
    if (this->propertyBuilder)
        return;

    this->propertyBuilder = std::make_shared<Mqtt5PropertyBuilder>();
}

Publish::Publish(const Publish &other) :
    PublishBase(other)
{

}

Publish::Publish(const std::string &topic, const std::string &payload, char qos) :
    PublishBase(topic, payload, qos)
{

}

bool WillDelayCompare(const std::shared_ptr<Publish> &a, const std::weak_ptr<Publish> &b)
{
    std::shared_ptr<Publish> _b = b.lock();

    if (!_b)
        return true;

    return a->will_delay < _b->will_delay;
};

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
