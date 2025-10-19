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

SubscriptionOptionsByte::SubscriptionOptionsByte(uint8_t qos, bool noLocal, bool retainAsPublished, RetainHandling retainHandling) :
    b(
        qos |
        (static_cast<uint8_t>(noLocal) << 2) |
        (static_cast<uint8_t>(retainAsPublished) << 3) |
        (static_cast<uint8_t>(retainHandling) << 4) )
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

RetainHandling SubscriptionOptionsByte::getRetainHandling() const
{
    uint8_t x = b & 0b00110000;
    x = x >> 4;

    if (x == 4)
        throw ProtocolError("Retain Handling value of 4 is protocol error", ReasonCodes::ProtocolError);

    return static_cast<RetainHandling>(x);
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

Publish::Publish(const std::string &topic, const std::string &payload, uint8_t qos) :
    topic(topic),
    payload(payload),
    qos(qos)
{

}

bool Publish::hasExpired() const
{
    if (!expireInfo)
        return false;

    return (getAge<std::chrono::milliseconds>() > expireInfo->expiresAfter);
}

std::vector<std::pair<std::string, std::string>> *Publish::getUserProperties() const
{
    return userProperties.get();
}

void Publish::addUserProperty(const std::string &key, const std::string &val)
{
    if (!userProperties)
        userProperties = std::make_shared<std::vector<std::pair<std::string, std::string>>>();

    userProperties->emplace_back(key, val);
}

void Publish::addUserProperty(std::string &&key, std::string &&val)
{
    if (!userProperties)
        userProperties = std::make_shared<std::vector<std::pair<std::string, std::string>>>();

    if (userProperties->size() > 50)
        throw ProtocolError("Trying to set more than 50 user properties. Likely a bad actor.", ReasonCodes::ImplementationSpecificError);

    userProperties->emplace_back(std::move(key), std::move(val));
}

std::optional<std::string> Publish::getFirstUserProperty(const std::string &key) const
{
    if (!userProperties)
        return {};

    auto pos = std::find_if(userProperties->begin(), userProperties->end(), [&key](const std::pair<std::string, std::string> &p) {
        return (p.first == key);
    });

    if (pos == userProperties->end())
        return {};

    return pos->second;
}

std::optional<Mqtt5PropertyBuilder> Publish::getPropertyBuilder() const
{
    std::optional<Mqtt5PropertyBuilder> property_builder;

    if (expireInfo)
        non_optional(property_builder)->writeMessageExpiryInterval(expireInfo->expiresAfter.count());

    if (correlationData)
        non_optional(property_builder)->writeCorrelationData(*correlationData);

    if (responseTopic)
        non_optional(property_builder)->writeResponseTopic(*responseTopic);

    if (contentType)
        non_optional(property_builder)->writeContentType(*contentType);

    if (payloadUtf8)
        non_optional(property_builder)->writePayloadFormatIndicator(1);

    if (topicAlias > 0)
        non_optional(property_builder)->writeTopicAlias(topicAlias);

    if (subscriptionIdentifier > 0)
        non_optional(property_builder)->writeSubscriptionIdentifier(subscriptionIdentifier);

    if (userProperties)
        non_optional(property_builder)->writeUserProperties(*userProperties);

    return property_builder;
}

void Publish::setExpireAfter(uint32_t s)
{
    this->expireInfo.emplace();
    this->expireInfo->expiresAfter = std::chrono::seconds(s);
}

void Publish::setExpireAfterToCeiling(std::chrono::seconds ceiling)
{
    if (expireInfo)
    {
        this->expireInfo->expiresAfter = std::min(this->expireInfo->expiresAfter, ceiling);
        return;
    }

    this->expireInfo.emplace();
    this->expireInfo->expiresAfter = ceiling;
}

const std::vector<std::string> &Publish::getSubtopics()
{
    if (!subtopics)
        subtopics = splitTopic(this->topic);

    return subtopics.value();
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

std::optional<Mqtt5PropertyBuilder> WillPublish::getPropertyBuilder() const
{
    auto property_builder = Publish::getPropertyBuilder();

    if (this->will_delay > 0)
        non_optional(property_builder)->writeWillDelay(this->will_delay);

    return property_builder;
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

std::string_view Connect::getMagicString() const
{
    if (protocolVersion <= ProtocolVersion::Mqtt31)
        return "MQIsdp";
    else
        return "MQTT";
}

Subscribe::Subscribe(const std::string &topic, uint8_t qos) :
    topic(topic),
    qos(qos)
{

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

std::chrono::time_point<std::chrono::steady_clock> PublishExpireInfo::expiresAt() const
{
    auto result = this->createdAt + this->expiresAfter;
    return result;
}

std::chrono::seconds PublishExpireInfo::getCurrentTimeToExpire() const
{
    const auto now = std::chrono::steady_clock::now();
    std::chrono::seconds delay = std::chrono::duration_cast<std::chrono::seconds>(now - createdAt);
    std::chrono::seconds newExpireAfter = std::max(expiresAfter - delay, std::chrono::seconds(0));
    return newExpireAfter;
}
