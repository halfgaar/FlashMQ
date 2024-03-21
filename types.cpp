/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include <cassert>

#include "types.h"
#include "mqtt5properties.h"
#include "exceptions.h"

#include "utils.h"

SubscriptionOptionsByte::SubscriptionOptionsByte(uint8_t byte) :
    b(byte)
{

}

SubscriptionOptionsByte::SubscriptionOptionsByte(uint8_t qos, bool noLocal, bool retainAsPublished) :
    b(qos | (static_cast<uint8_t>(noLocal) << 2) | (static_cast<uint8_t>(retainAsPublished) << 3))
{

}

bool SubscriptionOptionsByte::getNoLocal() const
{
    uint8_t bit = b & 0x04;
    return static_cast<bool>(bit);
}

bool SubscriptionOptionsByte::getRetainAsPublished() const
{
    uint8_t bit = b & 0x08;
    return static_cast<bool>(bit);
}

uint8_t SubscriptionOptionsByte::getQos() const
{
    uint8_t qos = b & 0x03;
    return static_cast<uint8_t>(qos);
}

ConnAck::ConnAck(const ProtocolVersion protVersion, ReasonCodes return_code, bool session_present) :
    protocol_version(protVersion),
    session_present(session_present)
{
    if (this->protocol_version <= ProtocolVersion::Mqtt311)
    {
        this->supported_reason_code = true;
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
            /*
             * The MQTT 3 says: "If none of the return codes listed in Table 3.1 – Connect Return code values are deemed applicable,
             * then the Server MUST close the Network Connection without sending a CONNACK"
             *
             * But throwing an exception here was removed, because it was too late, and it could bite us when trying we
             * were already in error handling code. So, you have the option to check the bool, but if you don't,
             * it just sends ServerUnavailable.
             */
            supported_reason_code = false;
            mqtt3_return = ConnAckReturnCodes::ServerUnavailable;
        }

        // [MQTT-3.2.2-4]
        if (mqtt3_return > ConnAckReturnCodes::Accepted)
            this->session_present = false;

        this->return_code = static_cast<uint8_t>(mqtt3_return);
    }
    else
    {
        this->supported_reason_code = true;
        this->return_code = static_cast<uint8_t>(return_code);

        // MQTT-3.2.2-6
        if (this->return_code > 0)
            this->session_present = false;
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

PublishBase::PublishBase(const std::string &topic, const std::string &payload, uint8_t qos) :
    topic(topic),
    payload(payload),
    qos(qos)
{

}

/**
 * @brief PublishBase::getLengthWithoutFixedHeader gets the size for packet buffer allocation, but without the MQTT5 properties.
 * @return
 *
 * The protocol version is not part of the Publish object, because it's used to send publishes to MQTT3 and MQTT5 clients, so that
 * has to be added later. See the `MqttPacket::MqttPacket(const ProtocolVersion protocolVersion, Publish &_publish)`
 * constructor.
 */
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
        std::chrono::seconds newExpireAfter = std::max(this->expiresAfter - delay, std::chrono::seconds(0));

        this->expiresAfter = newExpireAfter;
        propertyBuilder->writeMessageExpiryInterval(newExpireAfter.count());
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

    return (getAge() > expiresAfter);
}

std::chrono::seconds PublishBase::getAge() const
{
    if (!hasExpireInfo)
        return std::chrono::seconds(0);

    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - this->createdAt);
}

std::chrono::time_point<std::chrono::steady_clock> PublishBase::expiresAt() const
{
    auto result = this->createdAt + this->expiresAfter;
    return result;
}

std::vector<std::pair<std::string, std::string>> *PublishBase::getUserProperties()
{
    if (this->propertyBuilder)
        return this->propertyBuilder->getUserProperties().get();

    return nullptr;
}

void PublishBase::setExpireAfter(uint32_t s)
{
    this->createdAt = std::chrono::steady_clock::now();
    this->expiresAfter = std::chrono::seconds(s);
    this->hasExpireInfo = true;
}

void PublishBase::setExpireAfterToCeiling(uint32_t ceiling)
{
    std::chrono::seconds ceiling_s(ceiling);

    if (hasExpireInfo)
    {
        this->expiresAfter = std::min(this->expiresAfter, ceiling_s);
        return;
    }

    this->createdAt = std::chrono::steady_clock::now();
    this->expiresAfter = ceiling_s;
    this->hasExpireInfo = true;
}

bool PublishBase::getHasExpireInfo() const
{
    return this->hasExpireInfo;
}

std::chrono::seconds PublishBase::getExpiresAfter() const
{
    assert(hasExpireInfo);
    return this->expiresAfter;
}

std::chrono::time_point<std::chrono::steady_clock> PublishBase::getCreatedAt() const
{
    return this->createdAt;
}

Publish::Publish(const Publish &other) :
    PublishBase(other)
{

}

Publish::Publish(const std::string &topic, const std::string &payload, uint8_t qos) :
    PublishBase(topic, payload, qos)
{

}

Publish &Publish::operator=(const Publish &other)
{
    PublishBase::operator=(other);
    return *this;
}

const std::vector<std::string> &Publish::getSubtopics()
{
    if (subtopics.empty())
        subtopics = splitTopic(this->topic);

    return subtopics;
}

void Publish::resplitTopic()
{
    subtopics = splitTopic(this->topic);
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
    protocolVersion(protVersion),
    reasonCode(reason_code)
{

}

size_t Disconnect::getLengthWithoutFixedHeader() const
{
    if (this->protocolVersion < ProtocolVersion::Mqtt5)
        return 0;

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

Connect::Connect(ProtocolVersion protocolVersion, const std::string &clientid) :
    protocolVersion(protocolVersion),
    clientid(clientid)
{

}

size_t Connect::getLengthWithoutFixedHeader() const
{
    size_t result = clientid.length() + 2;

    result += this->protocolVersion <= ProtocolVersion::Mqtt31 ? 6 : 4;
    result += 6; // header stuff, lengths, keep-alive

    if (this->protocolVersion >= ProtocolVersion::Mqtt5)
    {
        const size_t proplen = propertyBuilder ? propertyBuilder->getLength() : 1;
        result += proplen;
    }

    if (will)
    {
        if (this->protocolVersion >= ProtocolVersion::Mqtt5)
        {
            const size_t proplen = will->propertyBuilder ? will->propertyBuilder->getLength() : 1;
            result += proplen;
        }

        result += will->topic.length() + 2;
        result += will->payload.length() + 2;
    }

    if (username.has_value())
        result += username->size() + 2;

    if (password.has_value())
        result += password->size() + 2;

    return result;
}

std::string_view Connect::getMagicString() const
{
    if (protocolVersion <= ProtocolVersion::Mqtt31)
        return "MQIsdp";
    else
        return "MQTT";
}

void Connect::constructPropertyBuilder()
{
    if (this->propertyBuilder)
        return;

    this->propertyBuilder = std::make_shared<Mqtt5PropertyBuilder>();
}

Subscribe::Subscribe(const ProtocolVersion protocolVersion, uint16_t packetId, const std::string &topic, uint8_t qos) :
    protocolVersion(protocolVersion),
    packetId(packetId),
    topic(topic),
    qos(qos)
{

}

size_t Subscribe::getLengthWithoutFixedHeader() const
{
    size_t result = topic.size() + 2;
    result += 2; // packet id
    result += 1; // requested QoS

    if (this->protocolVersion >= ProtocolVersion::Mqtt5)
    {
        const size_t proplen = propertyBuilder ? propertyBuilder->getLength() : 1;
        result += proplen;
    }

    return result;
}

Unsubscribe::Unsubscribe(const ProtocolVersion protocolVersion, uint16_t packetId, const std::string &topic) :
    protocolVersion(protocolVersion),
    packetId(packetId),
    topic(topic)
{

}

size_t Unsubscribe::getLengthWithoutFixedHeader() const
{
    size_t result = topic.size() + 2;
    result += 2; // packet id

    if (this->protocolVersion >= ProtocolVersion::Mqtt5)
    {
        const size_t proplen = propertyBuilder ? propertyBuilder->getLength() : 1;
        result += proplen;
    }

    return result;
}
