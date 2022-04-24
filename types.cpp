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
            throw ProtocolError("CONNACK error situation. As per MQTT3 spec, closing connection without sending any return code.");
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

SubAck::SubAck(const ProtocolVersion protVersion, uint16_t packet_id, const std::list<ReasonCodes> &subs_qos_reponses) :
    protocol_version(protVersion),
    packet_id(packet_id)
{
    assert(!subs_qos_reponses.empty());

    for (const ReasonCodes ack_code : subs_qos_reponses)
    {
        assert(protVersion >= ProtocolVersion::Mqtt311 || ack_code <= ReasonCodes::GrantedQoS2);

        ReasonCodes _ack_code = ack_code;
        if (protVersion < ProtocolVersion::Mqtt5 && ack_code >= ReasonCodes::UnspecifiedError)
            _ack_code = ReasonCodes::UnspecifiedError; // Equals Mqtt 3.1.1 'suback failure'

        responses.push_back(static_cast<ReasonCodes>(_ack_code));
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
    if (!hasExpireInfo && this->topicAlias == 0)
        return;

    if (propertyBuilder)
        propertyBuilder->clearClientSpecificBytes();
    else
        propertyBuilder = std::make_shared<Mqtt5PropertyBuilder>();

    if (hasExpireInfo)
    {
        auto now = std::chrono::steady_clock::now();
        std::chrono::seconds delay = std::chrono::duration_cast<std::chrono::seconds>(now - createdAt);
        int32_t newExpire = (this->expiresAfter - delay).count();
        if (newExpire > 0)
            propertyBuilder->writeMessageExpiryInterval(newExpire);
    }

    if (topicAlias > 0)
        propertyBuilder->writeTopicAlias(this->topicAlias);
}

void PublishBase::clearClientSpecificProperties()
{
    if (propertyBuilder)
        propertyBuilder->clearClientSpecificBytes();
}

void PublishBase::constructPropertyBuilder()
{
    if (this->propertyBuilder)
        return;

    this->propertyBuilder = std::make_shared<Mqtt5PropertyBuilder>();
}

bool PublishBase::hasUserProperties() const
{
    return this->propertyBuilder.operator bool() && this->propertyBuilder->getUserProperties().operator bool();
}

bool PublishBase::hasExpired() const
{
    if (!hasExpireInfo)
        return false;

    const std::chrono::seconds age = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - this->createdAt);
    return (expiresAfter > age);
}

void PublishBase::setExpireAfter(uint32_t s)
{
    this->createdAt = std::chrono::steady_clock::now();
    this->expiresAfter = std::chrono::seconds(s);
    this->hasExpireInfo = true;
}

bool PublishBase::getHasExpireInfo() const
{
    return this->hasExpireInfo;
}

const std::chrono::time_point<std::chrono::steady_clock> PublishBase::getCreatedAt() const
{
    return this->createdAt;
}

Publish::Publish(const Publish &other) :
    PublishBase(other)
{

}

Publish::Publish(const std::string &topic, const std::string &payload, char qos) :
    PublishBase(topic, payload, qos)
{

}

WillPublish::WillPublish(const Publish &other) :
    Publish(other)
{

}

void WillPublish::setQueuedAt()
{
    this->isQueued = true;
    this->queuedAt = std::chrono::steady_clock::now();
}

/**
 * @brief WillPublish::getQueuedAtAge gets the time ago in seconds when this will was queued. The time is set externally by the queue action.
 * @return
 *
 * This age is required when saving wills to disk, because the new will delay to set on load is not the original will delay, but minus the
 * elapsed time after queueing.
 */
uint32_t WillPublish::getQueuedAtAge() const
{
    if (!isQueued)
        return 0;

    const std::chrono::seconds age = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - this->queuedAt);
    return age.count();
}

PubResponse::PubResponse(const ProtocolVersion protVersion, const PacketType packet_type, ReasonCodes reason_code, uint16_t packet_id) :
    packet_type(packet_type),
    protocol_version(protVersion),
    reason_code(protVersion >= ProtocolVersion::Mqtt5 ? reason_code : ReasonCodes::Success),
    packet_id(packet_id)
{
    assert(packet_type == PacketType::PUBACK || packet_type == PacketType::PUBREC || packet_type == PacketType::PUBREL || packet_type == PacketType::PUBCOMP);
}

uint8_t PubResponse::getLengthIncludingFixedHeader() const
{
    return 2 + getRemainingLength();
}

uint8_t PubResponse::getRemainingLength() const
{
    // I'm leaving out the property length of 0: "If the Remaining Length is less than 4 there is no Property Length and the value of 0 is used"
    const uint8_t result = needsReasonCode() ? 3 : 2;
    return result;
}

/**
 * @brief "The Reason Code and Property Length can be omitted if the Reason Code is 0x00 (Success) and there are no Properties"
 * @return
 */
bool PubResponse::needsReasonCode() const
{
    return this->protocol_version >= ProtocolVersion::Mqtt5 && this->reason_code > ReasonCodes::Success;
}

UnsubAck::UnsubAck(const ProtocolVersion protVersion, uint16_t packet_id, const int unsubCount) :
    protocol_version(protVersion),
    packet_id(packet_id),
    reasonCodes(unsubCount)
{
    if (protVersion >= ProtocolVersion::Mqtt5)
    {
        // At this point, FlashMQ has no mechanism that would reject unsubscribes, so just marking them all as success.
        for(ReasonCodes &rc : this->reasonCodes)
        {
            rc = ReasonCodes::Success;
        }
    }
}

size_t UnsubAck::getLengthWithoutFixedHeader() const
{
    size_t result = 2; // Start with room for packet id

    if (this->protocol_version >= ProtocolVersion::Mqtt5)
    {
        result += this->reasonCodes.size();
        const size_t proplen = propertyBuilder ? propertyBuilder->getLength() : 1;
        result += proplen;
    }

    return result;
}

Disconnect::Disconnect(const ProtocolVersion protVersion, ReasonCodes reason_code) :
    reasonCode(reason_code)
{
    assert(protVersion >= ProtocolVersion::Mqtt5);


}

size_t Disconnect::getLengthWithoutFixedHeader() const
{
    size_t result = 1;
    const size_t proplen = propertyBuilder ? propertyBuilder->getLength() : 1;
    result += proplen;
    return result;
}

Auth::Auth(ReasonCodes reasonCode, const std::string &authMethod, const std::string &authData) :
    reasonCode(reasonCode),
    propertyBuilder(std::make_shared<Mqtt5PropertyBuilder>())
{
    if (!authMethod.empty())
        propertyBuilder->writeAuthenticationMethod(authMethod);
    if (!authData.empty())
        propertyBuilder->writeAuthenticationData(authData);
}

size_t Auth::getLengthWithoutFixedHeader() const
{
    size_t result = 1;
    const size_t proplen = propertyBuilder ? propertyBuilder->getLength() : 1;
    result += proplen;
    return result;
}







