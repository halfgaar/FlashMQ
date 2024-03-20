/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "mqttpacket.h"
#include <cstring>
#include <iostream>
#include <list>
#include <cassert>

#include "utils.h"
#include "threadglobals.h"
#include "subscriptionstore.h"
#include "mainapp.h"
#include "exceptions.h"
#include "acksender.h"

// constructor for parsing incoming packets
MqttPacket::MqttPacket(CirBuf &buf, size_t packet_len, size_t fixed_header_length, std::shared_ptr<Client> &sender) :
    bites(packet_len),
    fixed_header_length(fixed_header_length),
    sender(sender)
{
    assert(packet_len > 0);

    if (packet_len > sender->getMaxIncomingPacketSize())
    {
        throw ProtocolError("Incoming packet size exceeded.", ReasonCodes::PacketTooLarge);
    }

    buf.read(bites.data(), packet_len);

    protocolVersion = sender->getProtocolVersion();
    first_byte = bites[0];
    unsigned char _packetType = (first_byte & 0xF0) >> 4;
    packetType = (PacketType)_packetType;
    pos += fixed_header_length;
    externallyReceived = true;
}

MqttPacket::MqttPacket(const ConnAck &connAck) :
    bites(connAck.getLengthWithoutFixedHeader())
{
    packetType = PacketType::CONNACK;
    first_byte = static_cast<char>(packetType) << 4;
    writeByte(connAck.session_present & 0b00000001); // all connect-ack flags are 0, except session-present. [MQTT-3.2.2.1]
    writeByte(connAck.return_code);

    if (connAck.protocol_version >= ProtocolVersion::Mqtt5)
    {
        // TODO: don't include the reason string and user properties when it would increase the CONACK packet beyond the max packet size as determined by client.
        //       We don't send those at all momentarily, so there is no logic to prevent it.
        writeProperties(connAck.propertyBuilder);
    }

    calculateRemainingLength();
}

MqttPacket::MqttPacket(const SubAck &subAck) :
    bites(subAck.getLengthWithoutFixedHeader())
{
    packetType = PacketType::SUBACK;
    first_byte = static_cast<char>(packetType) << 4;
    writeUint16(subAck.packet_id);

    if (subAck.protocol_version >= ProtocolVersion::Mqtt5)
    {
        // TODO: don't include the reason string and user properties when it would increase the SUBACK packet beyond the max packet size as determined by client.
        //       We don't send those at all momentarily, so there is no logic to prevent it.
        writeProperties(subAck.propertyBuilder);
    }

    std::vector<char> returnList;
    returnList.reserve(subAck.responses.size());
    for (ReasonCodes code : subAck.responses)
    {
        returnList.push_back(static_cast<char>(code));
    }

    writeBytes(&returnList[0], returnList.size());
    calculateRemainingLength();
}

MqttPacket::MqttPacket(const UnsubAck &unsubAck) :
    bites(unsubAck.getLengthWithoutFixedHeader())
{
    packetType = PacketType::UNSUBACK;
    first_byte = static_cast<char>(packetType) << 4;
    writeUint16(unsubAck.packet_id);

    if (unsubAck.protocol_version >= ProtocolVersion::Mqtt5)
    {
        writeProperties(unsubAck.propertyBuilder);

        for(const ReasonCodes &rc : unsubAck.reasonCodes)
        {
            writeByte(static_cast<uint8_t>(rc));
        }
    }

    calculateRemainingLength();
}

size_t MqttPacket::setClientSpecificPropertiesAndGetRequiredSizeForPublish(const ProtocolVersion protocolVersion, Publish &publish) const
{
    size_t result = publish.getLengthWithoutFixedHeader();
    if (protocolVersion >= ProtocolVersion::Mqtt5)
    {
        publish.setClientSpecificProperties();

        const size_t proplen = publish.propertyBuilder ? publish.propertyBuilder->getLength() : 1;
        result += proplen;
    }
    return result;
}

/**
 * @brief Construct a packet for a specific protocol version.
 * @param protocolVersion is required here, and not on the Publish object, because publishes don't have a protocol until they are for a specific client.
 * @param _publish
 *
 * Important to note here is that there are two concepts here: writing the byte array for sending to clients, and setting the data in publishData. The latter
 * will only have stuff important for internal logic. In other words, it won't contain the payload.
 */
MqttPacket::MqttPacket(const ProtocolVersion protocolVersion, Publish &_publish) :
    bites(setClientSpecificPropertiesAndGetRequiredSizeForPublish(protocolVersion, _publish))
{
    if (_publish.topic.length() > 0xFFFF)
    {
        throw ProtocolError("Topic path too long.", ReasonCodes::ProtocolError);
    }

    this->protocolVersion = protocolVersion;

    this->publishData.client_id = _publish.client_id;
    this->publishData.username = _publish.username;

    if (!_publish.skipTopic)
        this->publishData.topic = _publish.topic;

    packetType = PacketType::PUBLISH;
    this->publishData.qos = _publish.qos;
    first_byte = static_cast<char>(packetType) << 4;
    first_byte |= (_publish.qos << 1);
    first_byte |= (static_cast<char>(_publish.retain) & 0b00000001);

    writeString(publishData.topic);

    if (publishData.qos)
    {
        // Reserve the space for the packet id, which will be assigned later.
        packet_id_pos = pos;
#ifndef NDEBUG
        char zero[2] = {0,0};
        writeBytes(zero, 2);
#else
        advancePos(2);
#endif
    }

    if (protocolVersion >= ProtocolVersion::Mqtt5)
    {
        // Step 1: make certain properties available as objects, because FlashMQ needs access to them for internal logic (only ACL checking at this point).
        if (_publish.hasUserProperties())
        {
            this->publishData.constructPropertyBuilder();
            this->publishData.propertyBuilder->setNewUserProperties(_publish.propertyBuilder->getUserProperties());
        }

        // Step 2: this line will make sure the whole byte array containing all properties as flat bytes is present in the 'bites' vector,
        // which is sent to the subscribers.
        writeProperties(_publish.propertyBuilder);

        // And again, even though the expiry info has been written in the byte array, we need to store them in the publish object too,
        // in case we use that, like when queueing QoS packets.
        if (_publish.getHasExpireInfo())
        {
            this->publishData.setExpireAfter(_publish.getExpiresAfter().count());
        }
    }

    payloadStart = pos;
    payloadLen = _publish.payload.length();

    writeBytes(_publish.payload.c_str(), _publish.payload.length());
    calculateRemainingLength();
}

MqttPacket::MqttPacket(const PubResponse &pubAck) :
    bites(pubAck.getLengthIncludingFixedHeader())
{
    this->protocolVersion = pubAck.protocol_version;

    fixed_header_length = 2;
    const uint8_t firstByteDefaultBits = pubAck.packet_type == PacketType::PUBREL ? 0b0010 : 0;
    this->first_byte = (static_cast<uint8_t>(pubAck.packet_type) << 4) | firstByteDefaultBits;
    writeByte(first_byte);
    writeByte(pubAck.getRemainingLength());
    this->packet_id_pos = this->pos;
    writeUint16(pubAck.packet_id);

    if (pubAck.needsReasonCode())
    {
        // TODO: don't include the reason string and user properties when it would increase the PUBACK/PUBREL/PUBCOMP packet beyond the max packet size as determined by client.
        //       We don't send those at all momentarily, so there is no logic to prevent it.
        writeByte(static_cast<uint8_t>(pubAck.reason_code));
    }
}

/**
 * @brief Constructor to create a disconnect packet. In normal server mode, only MQTT5 is supposed to do that (MQTT3 has no concept of server-initiated
 * disconnect packet). But, we also use it in the test client.
 * @param disconnect
 */
MqttPacket::MqttPacket(const Disconnect &disconnect) :
    bites(disconnect.getLengthWithoutFixedHeader())
{
    this->protocolVersion = disconnect.protocolVersion;

    packetType = PacketType::DISCONNECT;
    first_byte = static_cast<char>(packetType) << 4;

    if (this->protocolVersion >= ProtocolVersion::Mqtt5)
    {
        writeByte(static_cast<uint8_t>(disconnect.reasonCode));
        writeProperties(disconnect.propertyBuilder);
    }

    calculateRemainingLength();
}

MqttPacket::MqttPacket(const Auth &auth) :
    bites(auth.getLengthWithoutFixedHeader()),
    protocolVersion(ProtocolVersion::Mqtt5),
    packetType(PacketType::AUTH)
{
    first_byte = static_cast<char>(packetType) << 4;

    writeByte(static_cast<uint8_t>(auth.reasonCode));
    writeProperties(auth.propertyBuilder);
    calculateRemainingLength();
}

MqttPacket::MqttPacket(const Connect &connect) :
    bites(connect.getLengthWithoutFixedHeader()),
    protocolVersion(connect.protocolVersion),
    packetType(PacketType::CONNECT)
{
    first_byte = static_cast<char>(packetType) << 4;

    const std::string_view magicString = connect.getMagicString();
    writeString(magicString);

    uint8_t protocolVersionByte = static_cast<uint8_t>(protocolVersion);
    if (connect.bridgeProtocolBit && protocolVersion <= ProtocolVersion::Mqtt311) // MQTT5 uses subscription options for it.
        protocolVersionByte |= 0x80;
    writeByte(protocolVersionByte);

    uint8_t flags = connect.clean_start << 1;
    flags |= static_cast<unsigned int>(connect.username.has_value()) << 7;
    flags |= static_cast<unsigned int>(connect.password.has_value()) << 6;

    if (connect.will)
    {
        flags |= 4;
        flags |= (connect.will->qos << 3);
        flags |= (connect.will->retain << 5);
    }

    writeByte(flags);

    writeUint16(connect.keepalive);

    if (connect.protocolVersion >= ProtocolVersion::Mqtt5)
    {
        writeProperties(connect.propertyBuilder);
    }

    writeString(connect.clientid);

    if (connect.will)
    {
        if (connect.protocolVersion >= ProtocolVersion::Mqtt5)
        {
            writeProperties(connect.will->propertyBuilder);
        }

        writeString(connect.will->topic);
        writeString(connect.will->payload);
    }

    if (connect.username.has_value())
        writeString(connect.username.value());
    if (connect.password.has_value())
        writeString(connect.password.value());

    calculateRemainingLength();
}

MqttPacket::MqttPacket(const Subscribe &subscribe) :
    bites(subscribe.getLengthWithoutFixedHeader()),
    packetType(PacketType::SUBSCRIBE)
{
    first_byte = static_cast<char>(packetType) << 4;
    first_byte |= 2; // required reserved bit

    writeUint16(subscribe.packetId);

    if (subscribe.protocolVersion >= ProtocolVersion::Mqtt5)
    {
        writeProperties(subscribe.propertyBuilder);
    }

    writeString(subscribe.topic);

    if (subscribe.protocolVersion < ProtocolVersion::Mqtt5)
    {
        writeByte(subscribe.qos);
    }
    else
    {
        SubscriptionOptionsByte options(subscribe.qos, subscribe.noLocal, subscribe.retainAsPublished);
        writeByte(options.b);
    }

    calculateRemainingLength();
}

MqttPacket::MqttPacket(const Unsubscribe &unsubscribe) :
    bites(unsubscribe.getLengthWithoutFixedHeader()),
    packetType(PacketType::UNSUBSCRIBE)
{
#ifndef TESTING
    throw NotImplementedException("Code is only for testing.");
#endif

    first_byte = static_cast<char>(packetType) << 4;
    first_byte |= 2; // required reserved bit

    writeUint16(unsubscribe.packetId);

    if (unsubscribe.protocolVersion >= ProtocolVersion::Mqtt5)
    {
        writeProperties(unsubscribe.propertyBuilder);
    }

    writeString(unsubscribe.topic);

    calculateRemainingLength();
}

void MqttPacket::bufferToMqttPackets(CirBuf &buf, std::vector<MqttPacket> &packetQueueIn, std::shared_ptr<Client> &sender)
{
    while (buf.usedBytes() >= MQTT_HEADER_LENGH)
    {
        // Determine the packet length by decoding the variable length
        int remaining_length_i = 1; // index of 'remaining length' field is one after start.
        uint fixed_header_length = 1;
        size_t multiplier = 1;
        size_t packet_length = 0;
        unsigned char encodedByte = 0;
        do
        {
            fixed_header_length++;

            if (fixed_header_length > 5)
                throw ProtocolError("Packet signifies more than 5 bytes in variable length header. Invalid.", ReasonCodes::MalformedPacket);

            // This happens when you only don't have all the bytes that specify the remaining length.
            if (fixed_header_length > buf.usedBytes())
                return;

            encodedByte = buf.peakAhead(remaining_length_i++);
            packet_length += (encodedByte & 127) * multiplier;
            multiplier *= 128;
            if (multiplier > 128*128*128*128)
                throw ProtocolError("Malformed Remaining Length.", ReasonCodes::MalformedPacket);
        }
        while ((encodedByte & 128) != 0);
        packet_length += fixed_header_length;

        if (sender && !sender->getAuthenticated() && packet_length >= 1024*1024)
        {
            throw ProtocolError("An unauthenticated client sends a packet of 1 MB or bigger? Probably it's just random bytes.", ReasonCodes::ProtocolError);
        }

        const uint32_t size_limit = std::min<uint32_t>(sender->getMaxIncomingPacketSize(), ABSOLUTE_MAX_PACKET_SIZE);
        if (packet_length > size_limit)
        {
            std::ostringstream oss;
            oss << "Packet size " << packet_length << " exceeds the server limit of " << size_limit << " bytes";
            throw ProtocolError(oss.str(), ReasonCodes::PacketTooLarge);
        }

        if (packet_length <= buf.usedBytes())
        {
            packetQueueIn.emplace_back(buf, packet_length, fixed_header_length, sender);
        }
        else
            break;
    }
}

void MqttPacket::handle()
{
    // For clients that send packets before they even receive a connack.
    if (protocolVersion == ProtocolVersion::None)
        protocolVersion = sender->getProtocolVersion();

    // It may be a stale client. This is especially important for when a session is picked up by another client. The old client
    // may still have stale data in the buffer, causing action on the session otherwise.
    if (sender->isBeingDisconnected())
        return;

    if (packetType == PacketType::Reserved)
        throw ProtocolError("Packet type 0 specified, which is reserved and invalid.", ReasonCodes::MalformedPacket);

    if (!sender->getAuthenticated())
    {
        if (packetType == PacketType::AUTH && sender->getExtendedAuthenticationMethod().empty())
        {
            throw ProtocolError("You can't initiate first (extended) authentication with an AUTH packet.", ReasonCodes::ProtocolError);
        }

        if (!(packetType == PacketType::CONNECT || packetType == PacketType::AUTH || packetType == PacketType::DISCONNECT ||
              packetType == PacketType::PINGREQ || packetType == PacketType::PINGRESP || packetType == PacketType::CONNACK))
        {
            exceptionOnNonMqtt(this->bites);

            logger->log(LOG_WARNING) << "Unapproved packet type (" << packetTypeToString(packetType)
                                     << ") from non-authenticated client " << sender->repr() << ". Dropping packet.";
            return;
        }
    }

    if (packetType == PacketType::PUBLISH)
        handlePublish();
    else if (packetType == PacketType::PUBACK)
        handlePubAck();
    else if (packetType == PacketType::PUBREC)
        handlePubRec();
    else if (packetType == PacketType::PUBREL)
        handlePubRel();
    else if (packetType == PacketType::PUBCOMP)
        handlePubComp();
    else if (packetType == PacketType::PINGREQ)
        sender->writePingResp();
    else if (packetType == PacketType::SUBSCRIBE)
        handleSubscribe();
    else if (packetType == PacketType::UNSUBSCRIBE)
        handleUnsubscribe();
    else if (packetType == PacketType::SUBACK)
        handleSubAck();
    else if (packetType == PacketType::CONNECT)
        handleConnect();
    else if (packetType == PacketType::DISCONNECT)
        handleDisconnect();
    else if (packetType == PacketType::CONNACK)
        handleConnAck();
    else if (packetType == PacketType::AUTH)
        handleExtendedAuth();
}

ConnectData MqttPacket::parseConnectData()
{
    if (this->packetType != PacketType::CONNECT)
        throw std::runtime_error("Packet must be connect packet.");

    setPosToDataStart();

    ConnectData result;

    uint16_t variable_header_length = readTwoBytesToUInt16();

    if (!(variable_header_length == 4 || variable_header_length == 6))
    {
        throw ProtocolError("Invalid variable header length. Garbage?", ReasonCodes::MalformedPacket);
    }

    const Settings &settings = *ThreadGlobals::getSettings();

    const char *c = readBytes(variable_header_length);
    const std::string magic_marker(c, variable_header_length);

    const uint8_t protocolVersionByte = readUint8();
    result.protocol_level_byte = protocolVersionByte & 0x7F;
    result.bridge = protocolVersionByte & 0x80; // Unofficial, defacto, way of specifying that. MQTT5 uses subscription options for it.

    if (magic_marker == "MQTT")
    {
        if (result.protocol_level_byte == 0x04)
            protocolVersion = ProtocolVersion::Mqtt311;
        if (result.protocol_level_byte == 0x05)
            protocolVersion = ProtocolVersion::Mqtt5;
    }
    else if (magic_marker == "MQIsdp" && result.protocol_level_byte == 0x03)
    {
        protocolVersion = ProtocolVersion::Mqtt31;
    }
    else
    {
        throw ProtocolError("Packet contains invalid MQTT marker.", ReasonCodes::MalformedPacket);
    }

    // Even though we're still parsing, setting this helps the exception handler to make decisions.
    sender->setProtocolVersion(this->protocolVersion);

    char flagByte = readByte();
    bool reserved = !!(flagByte & 0b00000001);

    if (reserved)
        throw ProtocolError("Protocol demands reserved flag in CONNECT is 0", ReasonCodes::MalformedPacket);

    bool user_name_flag = static_cast<bool>(flagByte & 0b10000000);
    result.password_flag = !!(flagByte & 0b01000000);
    result.will_retain = !!(flagByte & 0b00100000);
    result.will_qos = (flagByte & 0b00011000) >> 3;
    result.will_flag = !!(flagByte & 0b00000100);
    result.clean_start = !!(flagByte & 0b00000010);

    if (result.will_qos > 2)
        throw ProtocolError("Invalid QoS for will.", ReasonCodes::MalformedPacket);

    result.keep_alive = readTwoBytesToUInt16();

    if (protocolVersion == ProtocolVersion::Mqtt5)
    {
        /*
         * MQTT5: "If the Session Expiry Interval is absent the value 0 is used. If it is set to 0, or is absent,
         * the Session ends when the Network Connection is closed."
         */
        result.session_expire = 0;

        result.keep_alive = std::max<uint16_t>(result.keep_alive, 5);

        const size_t proplen = decodeVariableByteIntAtPos();
        const size_t prop_end_at = pos + proplen;

        while (pos < prop_end_at)
        {
            const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readUint8());

            switch (prop)
            {
            case Mqtt5Properties::SessionExpiryInterval:
                result.session_expire = std::min<uint32_t>(readFourBytesToUint32(), settings.getExpireSessionAfterSeconds());
                break;
            case Mqtt5Properties::ReceiveMaximum:
                result.client_receive_max = std::min<int16_t>(readTwoBytesToUInt16(), result.client_receive_max);
                break;
            case Mqtt5Properties::MaximumPacketSize:
                result.max_outgoing_packet_size = std::min<uint32_t>(readFourBytesToUint32(), result.max_outgoing_packet_size);
                break;
            case Mqtt5Properties::TopicAliasMaximum:
                result.max_outgoing_topic_aliases = std::min<uint16_t>(readTwoBytesToUInt16(), settings.maxOutgoingTopicAliasValue);
                break;
            case Mqtt5Properties::RequestResponseInformation:
                result.request_response_information = !!readByte();
                break;
            case Mqtt5Properties::RequestProblemInformation:
                result.request_problem_information = !!readByte();
                break;
            case Mqtt5Properties::UserProperty:
                readUserProperty();
                break;
            case Mqtt5Properties::AuthenticationMethod:
            {
                result.authenticationMethod = readBytesToString();
                break;
            }
            case Mqtt5Properties::AuthenticationData:
            {
                result.authenticationData = readBytesToString(false);
                break;
            }
            default:
                throw ProtocolError("Invalid connect property.", ReasonCodes::ProtocolError);
            }
        }
    }

    if (result.client_receive_max == 0 || result.max_outgoing_packet_size == 0)
    {
        throw ProtocolError("Receive max or max outgoing packet size can't be 0.", ReasonCodes::ProtocolError);
    }

    result.client_id = readBytesToString();

    if (result.will_flag)
    {
        result.willpublish.qos = result.will_qos;
        result.willpublish.retain = result.will_retain;

        if (result.will_retain)
        {
            if (settings.retainedMessagesMode == RetainedMessagesMode::DisconnectWithError)
                throw ProtocolError("Option 'retained_messages_mode' set to 'disconnect_with_error' and received a will with retain.", ReasonCodes::RetainNotSupported);
            else if (settings.retainedMessagesMode == RetainedMessagesMode::Downgrade)
            {
                result.willpublish.retain = false;
                result.will_retain = false;
            }
            else if (settings.retainedMessagesMode == RetainedMessagesMode::Drop)
                result.will_flag = false; // This will make us not pick up later, and we still parse the bytes from the packet.
        }

        result.willpublish.client_id = result.client_id;

        if (protocolVersion == ProtocolVersion::Mqtt5)
        {
            result.willpublish.constructPropertyBuilder();

            const size_t proplen = decodeVariableByteIntAtPos();
            const size_t prop_end_at = pos + proplen;

            while (pos < prop_end_at)
            {
                const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readUint8());

                switch (prop)
                {
                case Mqtt5Properties::WillDelayInterval:
                    result.willpublish.will_delay = readFourBytesToUint32();
                    break;
                case Mqtt5Properties::PayloadFormatIndicator:
                    result.willpublish.propertyBuilder->writePayloadFormatIndicator(readByte());
                    break;
                case Mqtt5Properties::ContentType:
                {
                    const std::string contentType = readBytesToString();
                    result.willpublish.propertyBuilder->writeContentType(contentType);
                    break;
                }
                case Mqtt5Properties::ResponseTopic:
                {
                    const std::string responseTopic = readBytesToString(true, true);
                    result.willpublish.propertyBuilder->writeResponseTopic(responseTopic);
                    break;
                }
                case Mqtt5Properties::MessageExpiryInterval:
                {
                    const uint32_t expiresAfter = readFourBytesToUint32();
                    result.willpublish.setExpireAfter(expiresAfter);
                    break;
                }
                case Mqtt5Properties::CorrelationData:
                {
                    const std::string correlationData = readBytesToString(false);
                    result.willpublish.propertyBuilder->writeCorrelationData(correlationData);
                    break;
                }
                case Mqtt5Properties::UserProperty:
                {
                    result.willpublish.constructPropertyBuilder();

                    std::string key = readBytesToString();
                    std::string value = readBytesToString();

                    result.willpublish.propertyBuilder->writeUserProperty(std::move(key), std::move(value));
                    break;
                }
                default:
                    throw ProtocolError("Invalid will property in connect.", ReasonCodes::ProtocolError);
                }
            }
        }

        result.willpublish.topic = readBytesToString(true, true);

        uint16_t will_payload_length = readTwoBytesToUInt16();
        result.willpublish.payload = std::string(readBytes(will_payload_length), will_payload_length);
    }

    if (user_name_flag)
    {
        // Usernames must be UTF-8, but we defer that check so we can give proper a CONNACK, and continue parsing.
        result.username = readBytesToString(false);

        if (result.username.value().empty())
        {
            if (settings.zeroByteUsernameIsAnonymous)
                result.username.reset();
            else
                throw ProtocolError("Attempting anonymous login with zero byte username. See config option 'zero_byte_username_is_anonymous'.",
                                    ReasonCodes::BadUserNameOrPassword);
        }
    }

    if (result.username)
    {
        result.willpublish.username = result.username.value();

        if (!settings.allowUnsafeUsernameChars && containsDangerousCharacters(result.username.value()))
            throw ProtocolError(formatString("Username '%s' contains unsafe characters and 'allow_unsafe_username_chars' is false.", result.username.value().c_str()),
                                ReasonCodes::BadUserNameOrPassword);
    }

    if (result.password_flag)
    {
        if (this->protocolVersion <= ProtocolVersion::Mqtt311 && !user_name_flag)
        {
            throw ProtocolError("MQTT 3.1.1: If the User Name Flag is set to 0, the Password Flag MUST be set to 0.", ReasonCodes::MalformedPacket);
        }

        result.password = readBytesToString(false);
    }

    return  result;
}

ConnAckData MqttPacket::parseConnAckData()
{
    if (this->packetType != PacketType::CONNACK)
        throw std::runtime_error("Packet must be connack packet.");

    const Settings &settings = *ThreadGlobals::getSettings();

    setPosToDataStart();

    ConnAckData result;

    const uint8_t flagByte = readByte();

    result.sessionPresent = flagByte & 0x01;
    result.reasonCode = static_cast<ReasonCodes>(readUint8());

    if (protocolVersion == ProtocolVersion::Mqtt5)
    {
        const size_t proplen = decodeVariableByteIntAtPos();
        const size_t prop_end_at = pos + proplen;

        while (pos < prop_end_at)
        {
            const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readUint8());

            switch (prop)
            {
            case Mqtt5Properties::SessionExpiryInterval:
                result.session_expire = std::min<uint32_t>(readFourBytesToUint32(), result.session_expire);
                break;
            case Mqtt5Properties::ReceiveMaximum:
                result.client_receive_max = std::min<int16_t>(readTwoBytesToUInt16(), result.client_receive_max);
                break;
            case Mqtt5Properties::MaximumQoS:
                result.max_qos = std::min<uint8_t>(readUint8(), result.max_qos);
                break;
            case Mqtt5Properties::RetainAvailable:
                result.retained_available = static_cast<bool>(readByte());
                break;
            case Mqtt5Properties::MaximumPacketSize:
                result.max_outgoing_packet_size = std::min<uint32_t>(readFourBytesToUint32(), result.max_outgoing_packet_size);
                break;
            case Mqtt5Properties::AssignedClientIdentifier:
                result.assigned_client_id = readBytesToString();
                break;
            case Mqtt5Properties::TopicAliasMaximum:
                result.max_outgoing_topic_aliases = std::min<uint16_t>(readTwoBytesToUInt16(), settings.maxOutgoingTopicAliasValue);
                break;
            case Mqtt5Properties::ReasonString:
            {
                const std::string reason = readBytesToString();
                logger->logf(LOG_NOTICE, "ConnAck reason string: %s", reason.c_str());
                break;
            }
            case Mqtt5Properties::UserProperty:
            {
                std::string key = readBytesToString();
                std::string value = readBytesToString();
                break;
            }
            case Mqtt5Properties::WildcardSubscriptionAvailable:
                readByte();
                break;
            case Mqtt5Properties::SubscriptionIdentifierAvailable:
                readByte();
                break;
            case Mqtt5Properties::SharedSubscriptionAvailable:
                result.shared_subscriptions_available = !!readByte();
                break;
            case Mqtt5Properties::ServerKeepAlive:
                result.keep_alive = readTwoBytesToUInt16();
                break;
            case Mqtt5Properties::ResponseInformation:
                result.response_information = readBytesToString();
                break;
            case Mqtt5Properties::ServerReference:
                result.server_reference = readBytesToString();
                break;
            case Mqtt5Properties::AuthenticationMethod:
                result.authMethod = readBytesToString();
                break;
            case Mqtt5Properties::AuthenticationData:
                result.authData = readBytesToString();
                break;
            default:
                throw ProtocolError("Invalid connack property.", ReasonCodes::ProtocolError);
            }
        }
    }

    return result;
}

void MqttPacket::handleConnect()
{
    if (sender->hasConnectPacketSeen())
        throw ProtocolError("Client already sent a CONNECT.", ReasonCodes::ProtocolError);

    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();

    Authentication &authentication = *ThreadGlobals::getAuth();

    ThreadData *threadData = ThreadGlobals::getThreadData();
    threadData->mqttConnectCounter.inc();

    ConnectData connectData = parseConnectData();
    sender->setHasConnectPacketSeen();
    sender->setProtocolVersion(this->protocolVersion);

    sender->setBridge(connectData.bridge);

    if (this->protocolVersion == ProtocolVersion::None)
    {
        logger->logf(LOG_ERR, "Rejecting because of invalid protocol version: %s", sender->repr().c_str());

        // The specs are unclear when to use the version 3 codes or version 5 codes when you don't know which protocol version to speak.
        ProtocolVersion fuzzyProtocolVersion = connectData.protocol_level_byte < 0x05 ? ProtocolVersion::Mqtt31 : ProtocolVersion::Mqtt5;

        ConnAck connAck(fuzzyProtocolVersion, ReasonCodes::UnsupportedProtocolVersion);
        MqttPacket response(connAck);
        sender->setReadyForDisconnect();
        sender->writeMqttPacket(response);
        sender->setDisconnectReason("Unsupported protocol version");

        return;
    }

    const Settings &settings = *ThreadGlobals::getSettings();

    // I deferred the initial UTF8 check on username to be able to give an appropriate connack here, but to me, the specs
    // are actually vague whether 'BadUserNameOrPassword' should be given on invalid UTF8.
    if (connectData.username && !isValidUtf8(connectData.username.value()))
    {
        ConnAck connAck(protocolVersion, ReasonCodes::BadUserNameOrPassword);
        MqttPacket response(connAck);
        sender->setReadyForDisconnect();
        sender->writeMqttPacket(response);
        logger->logf(LOG_ERR, "Username has invalid UTF8: %s", connectData.username.value().c_str());
        return;
    }

    bool validClientId = true;

    // Check for wildcard chars in case the client_id ever appears in topics.
    if (!settings.allowUnsafeClientidChars && containsDangerousCharacters(connectData.client_id))
    {
        logger->logf(LOG_ERR, "ClientID '%s' has + or # in the id and 'allow_unsafe_clientid_chars' is false.", connectData.client_id.c_str());
        validClientId = false;
    }
    else if (protocolVersion < ProtocolVersion::Mqtt5 && !connectData.clean_start && connectData.client_id.empty())
    {
        logger->logf(LOG_ERR, "ClientID empty and clean start 0, which is incompatible below MQTTv5.");
        validClientId = false;
    }
    else if (protocolVersion < ProtocolVersion::Mqtt311 && connectData.client_id.empty())
    {
        logger->logf(LOG_ERR, "Empty clientID. Connect with protocol 3.1.1 or higher to have one generated securely.");
        validClientId = false;
    }

    if (!validClientId)
    {
        ConnAck connAck(protocolVersion, ReasonCodes::ClientIdentifierNotValid);
        MqttPacket response(connAck);
        sender->setDisconnectReason("Invalid clientID");
        sender->setReadyForDisconnect();
        sender->writeMqttPacket(response);
        return;
    }

    bool clientIdGenerated = false;
    if (connectData.client_id.empty())
    {
        connectData.client_id = getSecureRandomString(23);
        clientIdGenerated = true;
    }

    sender->setClientId(connectData.client_id);

    std::string username = connectData.username ? connectData.username.value() : "";

    if (sender->getX509ClientVerification() > X509ClientVerification::None)
    {
        std::optional<std::string> certificateUsername = sender->getUsernameFromPeerCertificate();

        if (!certificateUsername || certificateUsername.value().empty())
            throw ProtocolError("Client certificate did not provider username", ReasonCodes::BadUserNameOrPassword);

        username = certificateUsername.value();
    }

    sender->setClientProperties(protocolVersion, connectData.client_id, username, true, connectData.keep_alive,
                                connectData.max_outgoing_packet_size, connectData.max_outgoing_topic_aliases);

    if (settings.willsEnabled && connectData.will_flag)
    {
        sender->stageWill(std::move(connectData.willpublish));
    }

    // Stage connack, for immediate or delayed use when auth succeeds.
    {
        bool sessionPresent = false;
        std::shared_ptr<Session> existingSession;

        if (protocolVersion >= ProtocolVersion::Mqtt311 && !connectData.clean_start)
        {
            existingSession = subscriptionStore->lockSession(connectData.client_id);
            if (existingSession && !existingSession->getDestroyOnDisconnect())
                sessionPresent = true;
        }

        std::unique_ptr<ConnAck> connAck = std::make_unique<ConnAck>(protocolVersion, ReasonCodes::Success, sessionPresent);

        if (protocolVersion >= ProtocolVersion::Mqtt5)
        {
            connAck->propertyBuilder = std::make_shared<Mqtt5PropertyBuilder>();
            connAck->propertyBuilder->writeSessionExpiry(connectData.session_expire);
            connAck->propertyBuilder->writeReceiveMax(settings.maxQosMsgPendingPerClient);
            connAck->propertyBuilder->writeRetainAvailable(settings.retainedMessagesMode == RetainedMessagesMode::Enabled);
            connAck->propertyBuilder->writeMaxPacketSize(sender->getMaxIncomingPacketSize());
            if (clientIdGenerated)
                connAck->propertyBuilder->writeAssignedClientId(connectData.client_id);
            connAck->propertyBuilder->writeMaxTopicAliases(sender->getMaxIncomingTopicAliasValue());
            connAck->propertyBuilder->writeWildcardSubscriptionAvailable(1);
            connAck->propertyBuilder->writeSubscriptionIdentifiersAvailable(0);
            connAck->propertyBuilder->writeSharedSubscriptionAvailable(1);
            connAck->propertyBuilder->writeServerKeepAlive(connectData.keep_alive);

            if (!connectData.authenticationMethod.empty())
            {
                connAck->propertyBuilder->writeAuthenticationMethod(connectData.authenticationMethod);
            }
        }

        sender->stageConnack(std::move(connAck));
    }

    sender->setRegistrationData(connectData.clean_start, connectData.client_receive_max, connectData.session_expire);

    AuthResult authResult = AuthResult::login_denied;
    std::string authReturnData;

    bool allowAnonymous = settings.allowAnonymous;
    if (sender->getAllowAnonymousOverride() != AllowListenerAnonymous::None)
    {
        allowAnonymous = sender->getAllowAnonymousOverride() == AllowListenerAnonymous::Yes;
    }

    if (!connectData.username && connectData.authenticationMethod.empty() && allowAnonymous)
    {
        authResult = AuthResult::success;
    }
    else if (sender->getX509ClientVerification() == X509ClientVerification::X509IsEnough)
    {
        // The client will have been kicked out already if the certificate is not valid, so we can just approve it.
        authResult = AuthResult::success;
    }
    else if (connectData.authenticationMethod.empty())
    {
        authResult = authentication.unPwdCheck(connectData.client_id, username, connectData.password, getUserProperties(), sender, allowAnonymous);
    }
    else
    {
        sender->setExtendedAuthenticationMethod(connectData.authenticationMethod);

        authResult = authentication.extendedAuth(connectData.client_id, ExtendedAuthStage::Auth, connectData.authenticationMethod, connectData.authenticationData,
                                                 getUserProperties(), authReturnData, sender->getMutableUsername(), sender);
    }

    if (authResult != AuthResult::async)
    {
        threadData->continuationOfAuthentication(sender, authResult, connectData.authenticationMethod, authReturnData);
    }
}

void MqttPacket::handleConnAck()
{
    if (!sender->isOutgoingConnection())
        return;

    if (sender->hasConnectPacketSeen())
        throw ProtocolError("Client already sent a CONNACK.", ReasonCodes::ProtocolError);

    const Settings *settings = ThreadGlobals::getSettings();

    const ConnAckData data = parseConnAckData();

    if (data.reasonCode != ReasonCodes::Success)
    {
        throw std::runtime_error(formatString("Client '%s' connection failed. Reason code: %d", sender->repr().c_str(), data.reasonCode));
    }

    if (!settings->allowUnsafeClientidChars && containsDangerousCharacters(data.assigned_client_id))
    {
        const std::string error = formatString("Assigned clientID '%s' has + or # in the id and 'allow_unsafe_clientid_chars' is false.", data.assigned_client_id.c_str());
        throw ProtocolError(error, ReasonCodes::ImplementationSpecificError);
    }

    sender->setAuthenticated(true);

    std::shared_ptr<BridgeState> bridgeState = sender->getBridgeState();

    // Should be impossible.
    if (!bridgeState)
        return;

    bridgeState->resetReconnectCounter();

    logger->logf(LOG_NOTICE, "Bridge '%s' connection established. Subscribing to topics.", sender->repr().c_str());

    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();
    std::shared_ptr<Session> session = bridgeState->session.lock();

    // Should be impossible.
    if (!session)
        return;

    const uint16_t keepalive = data.keep_alive ? data.keep_alive : bridgeState->c.keepalive;

    const uint16_t effectiveMaxOutgoingTopicAliases = std::min<uint16_t>(data.max_outgoing_topic_aliases, bridgeState->c.maxOutgoingTopicAliases);

    const bool realRetainedAvailable = data.retained_available && bridgeState->c.remoteRetainAvailable;

    sender->setClientProperties(true, keepalive, data.max_outgoing_packet_size, effectiveMaxOutgoingTopicAliases, realRetainedAvailable);
    session->setSessionProperties(data.client_receive_max, bridgeState->c.localSessionExpiryInterval, bridgeState->c.localCleanStart, bridgeState->c.protocolVersion);

    ThreadGlobals::getThreadData()->queueClientNextKeepAliveCheckLocked(sender, true);

    // This resubscribes also when there is already a session with subscriptions remotely, but that is required when you change QoS levels, for instance. It
    // will not unsubscribe, so it will add to the existing subscriptions.
    // Note that this will also get you retained messages again.
    for(const BridgeTopicPath &sub : bridgeState->c.subscribes)
    {
        const uint8_t real_qos = std::min<uint8_t>(data.max_qos, sub.qos);

        logger->log(LOG_DEBUG) << "Bridge '" << sender->repr() << "' subscribing remotely to '" << sub.topic << "', QoS="
                               << static_cast<int>(real_qos) << ".";

        Subscribe s(this->getProtocolVersion(), session->getNextPacketIdLocked(), sub.topic, real_qos);
        s.noLocal = true;
        s.retainAsPublished = true;
        MqttPacket subPacket(s);
        sender->writeMqttPacketAndBlameThisClient(subPacket);
    }

    // It doesn't matter if we do this every time on connack; a client can only be subscribed once per pattern.
    // Note that this will also send all locally retained messages. I think that's the best approach: the state of retained
    // messages need to be synced; they may have been removed remotely while disconnected, for instance.
    for(const BridgeTopicPath &pub : bridgeState->c.publishes)
    {
        logger->log(LOG_DEBUG) << "Bridge '" << sender->repr() << "' subscribing locally to '" << pub.topic << "', QoS="
                               << static_cast<int>(pub.qos) << ".";

        const std::vector<std::string> subtopics = splitTopic(pub.topic);
        store->addSubscription(sender, subtopics, pub.qos, true, true);
    }

    ThreadGlobals::getThreadData()->publishBridgeState(bridgeState, true);

    session->sendAllPendingQosData();
}

AuthPacketData MqttPacket::parseAuthData()
{
    if (this->packetType != PacketType::AUTH)
        throw std::runtime_error("Packet must be an AUTH packet.");

    if (first_byte & 0b1111)
        throw ProtocolError("AUTH packet first 4 bits should be 0.", ReasonCodes::MalformedPacket);

    if (this->protocolVersion < ProtocolVersion::Mqtt5)
        throw ProtocolError("AUTH packet needs MQTT5 or higher", ReasonCodes::ProtocolError);

    setPosToDataStart();

    AuthPacketData result;

    result.reasonCode = static_cast<ReasonCodes>(readUint8());

    if (!atEnd())
    {
        const size_t proplen = decodeVariableByteIntAtPos();
        const size_t prop_end_at = pos + proplen;

        while (pos < prop_end_at)
        {
            const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readUint8());

            switch (prop)
            {
            case Mqtt5Properties::AuthenticationMethod:
                result.method = readBytesToString();
                break;
            case Mqtt5Properties::AuthenticationData:
                result.data = readBytesToString(false);
                break;
            case Mqtt5Properties::ReasonString:
                readBytesToString();
                break;
            case Mqtt5Properties::UserProperty:
                readUserProperty();
                break;
            default:
                throw ProtocolError("Invalid property in auth packet.", ReasonCodes::ProtocolError);
            }
        }
    }

    return result;
}

void MqttPacket::handleExtendedAuth()
{
    AuthPacketData data = parseAuthData();

    if (data.method != sender->getExtendedAuthenticationMethod())
        throw ProtocolError("Client continued with another authentication method that it started with.", ReasonCodes::ProtocolError);

    ExtendedAuthStage authStage = ExtendedAuthStage::None;

    switch(data.reasonCode)
    {
    case ReasonCodes::ContinueAuthentication:
        authStage = ExtendedAuthStage::Continue;
        break;
    case ReasonCodes::ReAuthenticate:
        authStage = ExtendedAuthStage::Reauth;
        break;
    default:
        throw ProtocolError(formatString("Invalid reason code '%d' in auth packet", static_cast<uint8_t>(data.reasonCode)), ReasonCodes::MalformedPacket);
    }

    if (authStage == ExtendedAuthStage::Reauth && !sender->getAuthenticated())
    {
        throw ProtocolError("Trying to reauth when client was not authenticated.", ReasonCodes::ProtocolError);
    }

    Authentication &authentication = *ThreadGlobals::getAuth();
    ThreadData *threadData = ThreadGlobals::getThreadData();

    std::string returnData;
    const AuthResult authResult = authentication.extendedAuth(sender->getClientId(), authStage, data.method, data.data,
                                                              getUserProperties(), returnData, sender->getMutableUsername(), sender);

    if (authResult != AuthResult::async)
    {
        threadData->continuationOfAuthentication(sender, authResult, data.method, returnData);
    }
}

DisconnectData MqttPacket::parseDisconnectData()
{
    if (this->packetType != PacketType::DISCONNECT)
        throw std::runtime_error("Packet must be disconnect packet.");

    if (first_byte & 0b1111)
        throw ProtocolError("Disconnect packet first 4 bits should be 0.", ReasonCodes::MalformedPacket);

    setPosToDataStart();

    DisconnectData result;

    if (this->protocolVersion >= ProtocolVersion::Mqtt5)
    {
        if (!atEnd())
            result.reasonCode = static_cast<ReasonCodes>(readUint8());

        if (!atEnd())
        {
            const size_t proplen = decodeVariableByteIntAtPos();
            const size_t prop_end_at = pos + proplen;

            while (pos < prop_end_at)
            {
                const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readUint8());

                switch (prop)
                {
                case Mqtt5Properties::SessionExpiryInterval:
                {
                    const Settings *settings = ThreadGlobals::getSettings();
                    const uint32_t session_expire = std::min<uint32_t>(readFourBytesToUint32(), settings->getExpireSessionAfterSeconds());
                    result.session_expiry_interval = session_expire;
                    result.session_expiry_interval_set = true;
                    break;
                }
                case Mqtt5Properties::ReasonString:
                {
                    result.reasonString = readBytesToString();
                    break;
                }
                case Mqtt5Properties::ServerReference:
                {
                    readBytesToString();
                    break;
                }
                case Mqtt5Properties::UserProperty:
                    readUserProperty();
                    break;
                default:
                    throw ProtocolError("Invalid property in disconnect.", ReasonCodes::ProtocolError);
                }
            }
        }
    }

    return result;
}

void MqttPacket::handleDisconnect()
{
    if (!sender)
        return;

    DisconnectData data = parseDisconnectData();

    std::string disconnectReason = "MQTT Disconnect received (reason '" + reasonCodeToString(data.reasonCode) + "').";

    if (!data.reasonString.empty())
        disconnectReason += data.reasonString;

    if (data.session_expiry_interval_set)
    {
        const std::shared_ptr<Session> session = sender->getSession();
        if (session)
            session->setSessionExpiryInterval(data.session_expiry_interval);
    }

    logger->logf(LOG_NOTICE, "Client '%s' cleanly disconnecting", sender->repr().c_str());
    sender->setDisconnectReason(disconnectReason);
    sender->markAsDisconnecting();
    if (data.reasonCode == ReasonCodes::Success)
        sender->clearWill();
    ThreadGlobals::getThreadData()->removeClientQueued(sender);
}

void MqttPacket::handleSubscribe()
{
    const char firstByteFirstNibble = (first_byte & 0x0F);

    if (firstByteFirstNibble != 2)
        throw ProtocolError("First LSB of first byte is wrong value for subscribe packet.", ReasonCodes::MalformedPacket);

    const uint16_t packet_id = readTwoBytesToUInt16();

    if (packet_id == 0)
    {
        throw ProtocolError("Packet ID 0 when subscribing is invalid.", ReasonCodes::MalformedPacket); // [MQTT-2.3.1-1]
    }

    if (protocolVersion == ProtocolVersion::Mqtt5)
    {
        const size_t proplen = decodeVariableByteIntAtPos();
        const size_t prop_end_at = pos + proplen;

        while (pos < prop_end_at)
        {
            const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readUint8());

            switch (prop)
            {
            case Mqtt5Properties::SubscriptionIdentifier:
                throw ProtocolError("Subscription identifiers not supported", ReasonCodes::ProtocolError);
            case Mqtt5Properties::UserProperty:
                readUserProperty();
                break;
            default:
                throw ProtocolError("Invalid subscribe property.", ReasonCodes::ProtocolError);
            }
        }
    }

    Authentication &authentication = *ThreadGlobals::getAuth();

    std::forward_list<SubscriptionTuple> deferredSubscribes;

    std::list<ReasonCodes> subs_reponse_codes;
    while (remainingAfterPos() > 0)
    {
        std::string topic = readBytesToString(true);

        const uint8_t qos_byte = readUint8();

        if (this->protocolVersion < ProtocolVersion::Mqtt5 && qos_byte > 2)
        {
            throw ProtocolError("QoS value in subscribe is higher than 2", ReasonCodes::MalformedPacket);
        }

        const SubscriptionOptionsByte options(qos_byte);

        uint8_t qos = options.getQos();

        const bool noLocal = sender->isBridge() || options.getNoLocal();
        const bool retainedAsPublished = sender->isBridge() || options.getRetainAsPublished();

        std::vector<std::string> subtopics = splitTopic(topic);

        if (authentication.alterSubscribe(sender->getClientId(), topic, subtopics, qos, getUserProperties()))
            subtopics = splitTopic(topic);

        if (topic.empty())
            throw ProtocolError("Subscribe topic is empty.", ReasonCodes::MalformedPacket);

        if (!isValidSubscribePath(topic))
            throw ProtocolError(formatString("Invalid subscribe path: %s", topic.c_str()), ReasonCodes::MalformedPacket);

        if (qos > 2)
            throw ProtocolError("QoS is greater than 2, and/or reserved bytes in QoS field are not 0.", ReasonCodes::MalformedPacket);

        std::string shareName;
        parseSubscriptionShare(subtopics, shareName);

        const Settings *settings = ThreadGlobals::getSettings();

        AuthResult authResult = AuthResult::success;

        if (settings->minimumWildcardSubscriptionDepth > 0 && getFirstWildcardDepth(subtopics) < settings->minimumWildcardSubscriptionDepth)
        {
            std::string action_text = "";

            if (settings->wildcardSubscriptionDenyMode == WildcardSubscriptionDenyMode::DenyRetainedOnly)
            {
                authResult = AuthResult::success_without_retained_delivery;
                action_text = "Denying retained messages";
            }
            else
            {
                authResult = AuthResult::acl_denied;
                action_text = "Denying subscription";
            }

            logger->log(LOG_WARNING) << "Wildcard subscription too broad. " << action_text << ". Topic: '"
                                     << topic << "'. Client: " << sender->repr();
        }

        if (authResult == AuthResult::success || authResult == AuthResult::success_without_retained_delivery)
        {
            const AuthResult newAuthResult = authentication.aclCheck(sender->getClientId(), sender->getUsername(), topic, subtopics, std::string_view(), AclAccess::subscribe, qos, false, getUserProperties());

            // We don't allow upgrading back to success. This gets too complicated between having no additional ACL, having an ACL file, and/or a plugin.
            if (newAuthResult != AuthResult::success)
                authResult = newAuthResult;
        }

        if (authResult == AuthResult::success || authResult == AuthResult::success_without_retained_delivery)
        {
            deferredSubscribes.emplace_front(topic, subtopics, qos, noLocal, retainedAsPublished, shareName, authResult);
            subs_reponse_codes.push_back(static_cast<ReasonCodes>(qos));
        }
        else
        {
            logger->logf(LOG_SUBSCRIBE, "Client '%s' subscribe to '%s' denied or failed.", sender->repr().c_str(), topic.c_str());

            // We can't not send an ack, because if there are multiple subscribes, you'd send fewer acks back, losing sync.
            ReasonCodes return_code = sender->getProtocolVersion() >= ProtocolVersion::Mqtt311 ? ReasonCodes::NotAuthorized : static_cast<ReasonCodes>(qos);
            subs_reponse_codes.push_back(return_code);
        }
    }

    // MQTT-3.8.3-3
    if (subs_reponse_codes.empty())
    {
        throw ProtocolError("No topics specified to subscribe to.", ReasonCodes::MalformedPacket);
    }

    SubAck subAck(this->protocolVersion, packet_id, subs_reponse_codes);
    MqttPacket response(subAck);
    sender->writeMqttPacket(response);

    // Adding the subscription will also send publishes for retained messages, so that's why we're doing it at the end.
    for(const SubscriptionTuple &tup : deferredSubscribes)
    {
        logger->logf(LOG_SUBSCRIBE, "Client '%s' subscribed to '%s' QoS %d", sender->repr().c_str(), tup.topic.c_str(), tup.qos);
        MainApp::getMainApp()->getSubscriptionStore()->addSubscription(sender, tup.subtopics, tup.qos, tup.noLocal, tup.retainAsPublished, tup.shareName, tup.authResult);
    }
}

void MqttPacket::handleSubAck()
{
    if (!sender->isOutgoingConnection())
        return;

    const SubAckData data = parseSubAckData();

    std::shared_ptr<BridgeState> bridgeState = sender->getBridgeState();

    // Should be impossible.
    if (!bridgeState)
        return;

    std::shared_ptr<Session> session = bridgeState->session.lock();

    // Should be impossible.
    if (!session)
        return;

    for(uint16_t id : data.subAckCodes)
    {
        (void)id;
        session->increaseFlowControlQuotaLocked();
    }
}

void MqttPacket::handleUnsubscribe()
{
    const char firstByteFirstNibble = (first_byte & 0x0F);

    if (firstByteFirstNibble != 2)
        throw ProtocolError("First LSB of first byte is wrong value for subscribe packet.", ReasonCodes::MalformedPacket);

    const uint16_t packet_id = readTwoBytesToUInt16();

    if (packet_id == 0)
    {
        throw ProtocolError("Packet ID 0 when unsubscribing is invalid.", ReasonCodes::ProtocolError); // [MQTT-2.3.1-1]
    }

    if (protocolVersion == ProtocolVersion::Mqtt5)
    {
        const size_t proplen = decodeVariableByteIntAtPos();
        const size_t prop_end_at = pos + proplen;

        while (pos < prop_end_at)
        {
            const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readUint8());

            switch (prop)
            {
            case Mqtt5Properties::UserProperty:
                readUserProperty();
                break;
            default:
                throw ProtocolError("Invalid unsubscribe property.", ReasonCodes::ProtocolError);
            }
        }
    }

    int numberOfUnsubs = 0;

    while (remainingAfterPos() > 0)
    {
        numberOfUnsubs++;

        std::string topic = readBytesToString();

        if (topic.empty())
            throw ProtocolError("Subscribe topic is empty.", ReasonCodes::MalformedPacket);

        MainApp::getMainApp()->getSubscriptionStore()->removeSubscription(sender, topic);
        logger->logf(LOG_UNSUBSCRIBE, "Client '%s' unsubscribed from '%s'", sender->repr().c_str(), topic.c_str());
    }

    // MQTT-3.10.3-2
    if (numberOfUnsubs == 0)
    {
        throw ProtocolError("No topics specified to unsubscribe to.", ReasonCodes::MalformedPacket);
    }

    UnsubAck unsubAck(this->sender->getProtocolVersion(), packet_id, numberOfUnsubs);
    MqttPacket response(unsubAck);
    sender->writeMqttPacket(response);
}

void MqttPacket::parsePublishData()
{
    assert(externallyReceived);

    setPosToDataStart();

    publishData.retain = (first_byte & 0b00000001);
    const bool duplicate = !!(first_byte & 0b00001000);
    publishData.qos = (first_byte & 0b00000110) >> 1;

    if (publishData.qos > 2)
        throw ProtocolError("QoS 3 is a protocol violation.", ReasonCodes::MalformedPacket);

    if (publishData.qos == 0 && duplicate)
        throw ProtocolError("Duplicate flag is set for QoS 0 packet. This is illegal.", ReasonCodes::MalformedPacket);

    publishData.username = sender->getUsername();
    publishData.client_id = sender->getClientId();

    publishData.topic = readBytesToString(true, true);

    if (publishData.qos)
    {
        packet_id_pos = pos;
        packet_id = readTwoBytesToUInt16();

        if (packet_id == 0)
        {
            throw ProtocolError("Packet ID 0 when publishing is invalid.", ReasonCodes::MalformedPacket); // [MQTT-2.3.1-1]
        }
    }

    if (this->protocolVersion >= ProtocolVersion::Mqtt5 )
    {
        const size_t proplen = decodeVariableByteIntAtPos();
        const size_t prop_end_at = pos + proplen;

        while (pos < prop_end_at)
        {
            const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readUint8());

            switch (prop)
            {
            case Mqtt5Properties::PayloadFormatIndicator:
                publishData.constructPropertyBuilder();
                publishData.propertyBuilder->writePayloadFormatIndicator(readByte());
                break;
            case Mqtt5Properties::MessageExpiryInterval:
                publishData.setExpireAfter(readFourBytesToUint32());
                break;
            case Mqtt5Properties::TopicAlias:
            {
                // For when we use packets has helpers without a senser (like loading packets from disk).
                // Logically, this should never trip because there can't be aliases in such packets, but including
                // a check to be sure.
                if (!sender)
                    break;

                const uint16_t alias_id = readTwoBytesToUInt16();
                this->hasTopicAlias = true;
                this->publishData.topicAlias = alias_id;

                if (publishData.topic.empty())
                {
                    publishData.topic = sender->getTopicAlias(alias_id);
                }
                else
                {
                    sender->setTopicAlias(alias_id, publishData.topic);
                }

                break;
            }
            case Mqtt5Properties::ResponseTopic:
            {
                publishData.constructPropertyBuilder();
                const std::string responseTopic = readBytesToString(true, true);
                publishData.propertyBuilder->writeResponseTopic(responseTopic);
                break;
            }
            case Mqtt5Properties::CorrelationData:
            {
                publishData.constructPropertyBuilder();
                const std::string correlationData = readBytesToString(false);
                publishData.propertyBuilder->writeCorrelationData(correlationData);
                break;
            }
            case Mqtt5Properties::UserProperty:
            {
                readUserProperty();
                break;
            }
            case Mqtt5Properties::SubscriptionIdentifier:
            {
                decodeVariableByteIntAtPos();
                break;
            }
            case Mqtt5Properties::ContentType:
            {
                publishData.constructPropertyBuilder();
                const uint16_t len = readTwoBytesToUInt16();
                const std::string contentType(readBytes(len), len);
                publishData.propertyBuilder->writeContentType(contentType);
                break;
            }
            default:
                throw ProtocolError("Invalid property in publish.", ReasonCodes::ProtocolError);
            }
        }
    }

    if (publishData.topic.empty())
        throw ProtocolError("Empty publish topic", ReasonCodes::ProtocolError);

    payloadLen = remainingAfterPos();
    payloadStart = pos;
}

void MqttPacket::handlePublish()
{
    parsePublishData();

#ifndef NDEBUG
    const bool duplicate = !!(first_byte & 0b00001000);
    logger->log(LOG_DEBUG) << "Publish received. Size: " << bites.size() << ". Topic: '" << publishData.topic << "'. QoS=" << static_cast<int>(publishData.qos)
                           << ". Retain=" << publishData.retain << ". Dup=" << duplicate << ". Alias=" << publishData.topicAlias << ".";
#endif

    ThreadGlobals::getThreadData()->receivedMessageCounter.inc();

    Authentication &authentication = *ThreadGlobals::getAuth();
    const Settings *settings = ThreadGlobals::getSettings();

    // Working with a local copy because the subscribing action will modify this->packet_id. See the PublishCopyFactory.
    const uint16_t _packet_id = this->packet_id;

    // Stage the ack, with the proper ID.
    AckSender ackSender(this->publishData.qos, this->packet_id, this->protocolVersion, sender);

    if (publishData.retain && settings->retainedMessagesMode == RetainedMessagesMode::DisconnectWithError)
    {
        throw ProtocolError("Retained messages not supported and 'retained_messages_mode' set to 'disconnect_with_error'.", ReasonCodes::RetainNotSupported);
    }

    if (publishData.qos == 2 && sender->getSession()->incomingQoS2MessageIdInTransit(_packet_id))
    {
        ackSender.setAckCode(ReasonCodes::PacketIdentifierInUse);
    }
    else
    {
        // Doing this before the authentication on purpose, so when the publish is not allowed, the QoS control packets are allowed and can finish.
        if (publishData.qos == 2)
            sender->getSession()->addIncomingQoS2MessageId(_packet_id);

        this->alteredByPlugin = authentication.alterPublish(this->publishData.client_id, this->publishData.topic, this->publishData.getSubtopics(),
                                                            getPayloadView(), this->publishData.qos, this->publishData.retain, this->publishData.getUserProperties());

        if (authentication.aclCheck(this->publishData, getPayloadView()) == AuthResult::success)
        {
            if (publishData.retain && settings->retainedMessagesMode == RetainedMessagesMode::Enabled)
            {
                publishData.payload = getPayloadCopy();
                MainApp::getMainApp()->getSubscriptionStore()->setRetainedMessage(publishData, publishData.getSubtopics());
            }

            if (!publishData.retain || settings->retainedMessagesMode <= RetainedMessagesMode::Downgrade)
            {
                // Set dup flag to 0, because that must not be propagated [MQTT-3.3.1-3].
                bites[0] &= 0b11110111;
                first_byte = bites[0];

                PublishCopyFactory factory(this);

                if (settings->sharedSubscriptionTargeting == SharedSubscriptionTargeting::SenderHash)
                {
                    const size_t senderHash = std::hash<std::string>()(sender->getClientId());
                    factory.setSharedSubscriptionHashKey(senderHash);
                }

                ackSender.sendNow();
                MainApp::getMainApp()->getSubscriptionStore()->queuePacketAtSubscribers(factory, sender->getClientId());
            }
        }
        else
        {
            ackSender.setAckCode(ReasonCodes::NotAuthorized);
        }
    }

#ifndef NDEBUG
    // Protection against using the altered packet id (because we change the incoming byte array for each subscriber).
    this->packet_id = 0;
    this->publishData.qos = 0;
#endif
}

void MqttPacket::parsePubAckData()
{
    setPosToDataStart();
    this->publishData.qos = 1;
    this->packet_id = readTwoBytesToUInt16();

    if (this->packet_id == 0)
        throw ProtocolError("QoS packets must have packet ID > 0.", ReasonCodes::ProtocolError);
}

void MqttPacket::handlePubAck()
{
    parsePubAckData();
    sender->getSession()->clearQosMessage(packet_id, true);
}

PubRecData MqttPacket::parsePubRecData()
{
    setPosToDataStart();
    this->publishData.qos = 2;
    this->packet_id = readTwoBytesToUInt16();

    if (this->packet_id == 0)
        throw ProtocolError("QoS packets must have packet ID > 0.", ReasonCodes::ProtocolError);

    PubRecData result;

    if (!atEnd())
    {
        result.reasonCode = static_cast<ReasonCodes>(readUint8());
    }

    return result;
}

/**
 * @brief MqttPacket::handlePubRec handles QoS 2 'publish received' packets. The publisher receives these.
 */
void MqttPacket::handlePubRec()
{
    PubRecData data = parsePubRecData();

    const bool publishTerminatesHere = data.reasonCode >= ReasonCodes::UnspecifiedError;
    const bool foundAndRemoved = sender->getSession()->clearQosMessage(packet_id, publishTerminatesHere);

    // "If it has sent a PUBREC with a Reason Code of 0x80 or greater, the receiver MUST treat any subsequent PUBLISH packet
    // that contains that Packet Identifier as being a new Application Message."
    if (!publishTerminatesHere)
    {
        sender->getSession()->addOutgoingQoS2MessageId(packet_id);

        // MQTT5: "[The sender] MUST send a PUBREL packet when it receives a PUBREC packet from the receiver with a Reason Code value less than 0x80"
        const ReasonCodes reason = foundAndRemoved ? ReasonCodes::Success : ReasonCodes::PacketIdentifierNotFound;
        PubResponse pubRel(this->protocolVersion, PacketType::PUBREL, reason, packet_id);
        MqttPacket response(pubRel);
        sender->writeMqttPacket(response);
    }
}

void MqttPacket::parsePubRelData()
{
    // MQTT-3.6.1-1, but why do we care, and only care for certain control packets?
    if (first_byte & 0b1101)
        throw ProtocolError("PUBREL first byte LSB must be 0010.", ReasonCodes::MalformedPacket);

    setPosToDataStart();
    this->publishData.qos = 2;
    this->packet_id = readTwoBytesToUInt16();
}

/**
 * @brief MqttPacket::handlePubRel handles QoS 2 'publish release'. The publisher sends these.
 */
void MqttPacket::handlePubRel()
{
    parsePubRelData();

    if (this->packet_id == 0)
        throw ProtocolError("QoS packets must have packet ID > 0.", ReasonCodes::ProtocolError);

    const bool foundAndRemoved = sender->getSession()->removeIncomingQoS2MessageId(packet_id);
    const ReasonCodes reason = foundAndRemoved ? ReasonCodes::Success : ReasonCodes::PacketIdentifierNotFound;

    PubResponse pubcomp(this->protocolVersion, PacketType::PUBCOMP, reason, packet_id);
    MqttPacket response(pubcomp);
    sender->writeMqttPacket(response);
}

void MqttPacket::parsePubComp()
{
    setPosToDataStart();
    this->publishData.qos = 2;
    this->packet_id = readTwoBytesToUInt16();

    if (this->packet_id == 0)
        throw ProtocolError("QoS packets must have packet ID > 0.", ReasonCodes::ProtocolError);
}

/**
 * @brief MqttPacket::handlePubComp handles QoS 2 'publish complete'. The publisher receives these.
 */
void MqttPacket::handlePubComp()
{
    parsePubComp();
    sender->getSession()->removeOutgoingQoS2MessageId(packet_id);
}

SubAckData MqttPacket::parseSubAckData()
{
    if (this->packetType != PacketType::SUBACK)
        throw std::runtime_error("Packet must be suback packet.");

    setPosToDataStart();

    SubAckData result;

    result.packet_id = readTwoBytesToUInt16();
    this->packet_id = result.packet_id;

    if (this->protocolVersion >= ProtocolVersion::Mqtt5 )
    {
        const size_t proplen = decodeVariableByteIntAtPos();
        const size_t prop_end_at = pos + proplen;

        while (pos < prop_end_at)
        {
            const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readUint8());

            switch (prop)
            {
            case Mqtt5Properties::ReasonString:
                result.reasonString = readBytesToString();
                break;
            case Mqtt5Properties::UserProperty:
                readUserProperty();
                break;
            default:
                throw ProtocolError("Invalid property in suback.", ReasonCodes::ProtocolError);
            }
        }
    }

    // payload starts here

    while (!atEnd())
    {
        uint8_t code = readByte();
        result.subAckCodes.push_back(code);
    }

    return result;
}

void MqttPacket::calculateRemainingLength()
{
    assert(fixed_header_length == 0); // because you're not supposed to call this on packet that we already know the length of.
    this->remainingLength = bites.size();
}

void MqttPacket::setPosToDataStart()
{
    this->pos = this->fixed_header_length;
}

bool MqttPacket::atEnd() const
{
    assert(pos <= bites.size());
    return pos >= bites.size();
}

void MqttPacket::setPacketId(uint16_t packet_id)
{
    assert(fixed_header_length == 0 || first_byte == bites[0]);
    assert(packet_id_pos > 0);
    assert(packetType == PacketType::PUBLISH);
    assert(publishData.qos > 0);

    this->packet_id = packet_id;
    pos = packet_id_pos;
    writeUint16(packet_id);
}

uint16_t MqttPacket::getPacketId() const
{
    assert(publishData.qos > 0);
    return packet_id;
}

// If I read the specs correctly, the DUP flag is merely for show. It doesn't control anything?
void MqttPacket::setDuplicate()
{
    assert(packetType == PacketType::PUBLISH);
    assert(publishData.qos > 0);
    assert(fixed_header_length == 0 || first_byte == bites[0]);

    first_byte |= 0b00001000;

    if (fixed_header_length > 0)
    {
        pos = 0;
        writeByte(first_byte);
    }
}

/**
 * @brief MqttPacket::getPayloadCopy takes part of the vector of bytes and returns it as a string.
 * @return
 */
std::string MqttPacket::getPayloadCopy() const
{
    assert(payloadStart > 0);
    assert(pos <= bites.size());
    std::string payload(&bites[payloadStart], payloadLen);
    return payload;
}

std::string_view MqttPacket::getPayloadView() const
{
    assert(payloadStart > 0);
    assert(pos <= bites.size());
    std::string_view payload(&bites[payloadStart], payloadLen);
    return payload;
}


uint8_t MqttPacket::getFixedHeaderLength() const
{
    size_t result = this->fixed_header_length;

    if (result == 0)
    {
        result++; // first byte it always there.
        result += remainingLength.getLen();
    }

    return result;
}

size_t MqttPacket::getSizeIncludingNonPresentHeader() const
{
    size_t total = bites.size();

    if (fixed_header_length == 0)
    {
        total += getFixedHeaderLength();
    }

    return total;
}

void MqttPacket::setQos(const uint8_t new_qos)
{
    // You can't change to a QoS level that would remove the packet identifier.
    assert((publishData.qos == 0 && new_qos == 0) || (publishData.qos > 0 && new_qos > 0));
    assert(new_qos > 0 && packet_id_pos > 0);

    publishData.qos = new_qos;
    first_byte &= 0b11111001;
    first_byte |= (publishData.qos << 1);

    if (fixed_header_length > 0)
    {
        pos = 0;
        writeByte(first_byte);
    }
}

const std::string &MqttPacket::getTopic() const
{
    return this->publishData.topic;
}

const std::vector<std::string> &MqttPacket::getSubtopics()
{
    return this->publishData.getSubtopics();
}

std::shared_ptr<Client> MqttPacket::getSender() const
{
    return sender;
}

void MqttPacket::setSender(const std::shared_ptr<Client> &value)
{
    sender = value;
}

bool MqttPacket::containsFixedHeader() const
{
    return fixed_header_length > 0;
}

void MqttPacket::advancePos(size_t len)
{
    if (pos + len > bites.size())
        throw ProtocolError("Exceeding packet size", ReasonCodes::MalformedPacket);

    pos += len;
}

char *MqttPacket::readBytes(size_t length)
{
    if (pos + length > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.", ReasonCodes::MalformedPacket);

    char *b = &bites[pos];
    pos += length;
    return b;
}

char MqttPacket::readByte()
{
    if (pos + 1 > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.", ReasonCodes::MalformedPacket);

    char b = bites[pos++];
    return b;
}

uint8_t MqttPacket::readUint8()
{
    char r = readByte();
    return static_cast<uint8_t>(r);
}

void MqttPacket::writeByte(char b)
{
    if (pos + 1 > bites.size())
        throw ProtocolError("Exceeding packet size", ReasonCodes::MalformedPacket);

    bites[pos++] = b;
}

void MqttPacket::writeUint16(uint16_t x)
{
    if (pos + 2 > bites.size())
        throw ProtocolError("Exceeding packet size", ReasonCodes::MalformedPacket);

    const uint8_t a = static_cast<uint8_t>(x >> 8);
    const uint8_t b = static_cast<uint8_t>(x);

    bites[pos++] = a;
    bites[pos++] = b;
}

void MqttPacket::writeBytes(const char *b, size_t len)
{
    if (pos + len > bites.size())
        throw ProtocolError("Exceeding packet size", ReasonCodes::MalformedPacket);

    memcpy(&bites[pos], b, len);
    pos += len;
}

void MqttPacket::writeProperties(const std::shared_ptr<Mqtt5PropertyBuilder> &properties)
{
    if (!properties)
        writeByte(0);
    else
    {
        writeVariableByteInt(properties->getVarInt());
        const std::vector<char> &b = properties->getGenericBytes();
        writeBytes(b.data(), b.size());
        const std::vector<char> &b2 = properties->getclientSpecificBytes();
        writeBytes(b2.data(), b2.size());
    }
}

void MqttPacket::writeVariableByteInt(const VariableByteInt &v)
{
    writeBytes(v.data(), v.getLen());
}

void MqttPacket::writeString(const std::string &s)
{
    writeUint16(s.length());
    writeBytes(s.c_str(), s.length());
}

void MqttPacket::writeString(std::string_view s)
{
    writeUint16(s.length());
    writeBytes(s.data(), s.length());
}

uint16_t MqttPacket::readTwoBytesToUInt16()
{
    if (pos + 2 > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.", ReasonCodes::MalformedPacket);

    uint8_t a = bites[pos];
    uint8_t b = bites[pos+1];
    uint16_t i = a << 8 | b;
    pos += 2;
    return i;
}

uint32_t MqttPacket::readFourBytesToUint32()
{
    if (pos + 4 > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.", ReasonCodes::MalformedPacket);

    const uint8_t a = bites[pos++];
    const uint8_t b = bites[pos++];
    const uint8_t c = bites[pos++];
    const uint8_t d = bites[pos++];
    uint32_t i = (a << 24) | (b << 16) | (c << 8) | d;
    return i;
}

size_t MqttPacket::remainingAfterPos()
{
    assert(pos <= bites.size());
    return bites.size() - pos;
}

size_t MqttPacket::decodeVariableByteIntAtPos()
{
    uint64_t multiplier = 1;
    size_t value = 0;
    uint8_t encodedByte = 0;
    do
    {
        if (pos >= bites.size())
            throw ProtocolError("Variable byte int length goes out of packet. Corrupt.", ReasonCodes::MalformedPacket);

        encodedByte = bites[pos++];
        value += (encodedByte & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128*128*128*128)
            throw ProtocolError("Malformed Remaining Length.", ReasonCodes::MalformedPacket);
    }
    while ((encodedByte & 128) != 0);

    return value;
}

void MqttPacket::readUserProperty()
{
    this->publishData.constructPropertyBuilder();

    std::string key = readBytesToString();
    std::string value = readBytesToString();

    this->publishData.propertyBuilder->writeUserProperty(std::move(key), std::move(value));
}

std::string MqttPacket::readBytesToString(bool validateUtf8, bool alsoCheckInvalidPublishChars)
{
    const uint16_t len = readTwoBytesToUInt16();
    std::string result(readBytes(len), len);

    if (validateUtf8)
    {
        if (!isValidUtf8(result, alsoCheckInvalidPublishChars))
        {
            logger->logf(LOG_DEBUG, "Data of invalid UTF-8 string or publish topic: %s", result.c_str());
            throw ProtocolError("Invalid UTF8 string detected, or invalid publish characters.", ReasonCodes::MalformedPacket);
        }
    }

    return result;
}

std::vector<std::pair<std::string, std::string>> *MqttPacket::getUserProperties()
{
    return this->publishData.getUserProperties();
}

bool MqttPacket::getRetain() const
{
    return (first_byte & 0b00000001);
}

void MqttPacket::setRetain(bool val)
{
    first_byte &= 0b11111110;
    first_byte |= static_cast<uint8_t>(val);

    if (fixed_header_length > 0)
    {
        pos = 0;
        writeByte(first_byte);
    }
}

const Publish &MqttPacket::getPublishData()
{
    if (payloadLen > 0 && publishData.payload.empty())
        publishData.payload = getPayloadCopy();

    return publishData;
}

bool MqttPacket::containsClientSpecificProperties() const
{
    assert(packetType == PacketType::PUBLISH);
    assert(this->externallyReceived);

    if (protocolVersion <= ProtocolVersion::Mqtt311)
        return false;

    // TODO: for the on-line clients, even with expire info, we can just copy the same packet. So, that case can be excluded.
    if (publishData.getHasExpireInfo() || this->hasTopicAlias)
    {
        return true;
    }

    return false;
}

/**
 * @brief MqttPacket::isAlteredByPlugin indicates the parsed data in PublishData no longer matches the byte array of the original incoming packet.
 * @return
 */
bool MqttPacket::isAlteredByPlugin() const
{
    return this->alteredByPlugin;
}

void MqttPacket::readIntoBuf(CirBuf &buf) const
{
    assert(packetType != PacketType::PUBLISH || (first_byte & 0b00000110) >> 1 == publishData.qos);
    assert(publishData.qos == 0 || packet_id > 0);

    buf.ensureFreeSpace(getSizeIncludingNonPresentHeader());

    if (!containsFixedHeader())
    {
        buf.headPtr()[0] = first_byte;
        buf.advanceHead(1);
        remainingLength.readIntoBuf(buf);
    }
    else
    {
        assert(bites.data()[0] == first_byte);
    }

    buf.write(bites.data(), bites.size());
}

SubscriptionTuple::SubscriptionTuple(const std::string &topic, const std::vector<std::string> &subtopics, uint8_t qos, bool noLocal, bool retainAsPublished,
                                     const std::string &shareName, const AuthResult authResult) :
    topic(topic),
    subtopics(subtopics),
    qos(qos),
    noLocal(noLocal),
    retainAsPublished(retainAsPublished),
    shareName(shareName),
    authResult(authResult)
{

}











