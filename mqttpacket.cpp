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

#include "mqttpacket.h"
#include <cstring>
#include <iostream>
#include <list>
#include <cassert>

#include "utils.h"
#include "threadglobals.h"

// constructor for parsing incoming packets
MqttPacket::MqttPacket(CirBuf &buf, size_t packet_len, size_t fixed_header_length, std::shared_ptr<Client> &sender) :
    bites(packet_len),
    fixed_header_length(fixed_header_length),
    sender(sender)
{
    assert(packet_len > 0);
    buf.read(bites.data(), packet_len);

    protocolVersion = sender->getProtocolVersion();
    first_byte = bites[0];
    unsigned char _packetType = (first_byte & 0xF0) >> 4;
    packetType = (PacketType)_packetType;
    pos += fixed_header_length;
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
    packetType = PacketType::SUBACK;
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

size_t MqttPacket::getRequiredSizeForPublish(const ProtocolVersion protocolVersion, const Publish &publish) const
{
    size_t result = publish.getLengthWithoutFixedHeader();
    if (protocolVersion >= ProtocolVersion::Mqtt5)
    {
        const size_t proplen = publish.propertyBuilder ? publish.propertyBuilder->getLength() : 1;
        result += proplen;
    }
    return result;
}

MqttPacket::MqttPacket(const ProtocolVersion protocolVersion, const Publish &_publish) :
    bites(getRequiredSizeForPublish(protocolVersion, _publish))
{
    if (publishData.topic.length() > 0xFFFF)
    {
        throw ProtocolError("Topic path too long.");
    }

    this->protocolVersion = protocolVersion;

    if (!_publish.skipTopic)
        this->publishData.topic = _publish.topic;

    // We often don't need to split because we already did the ACL checks and subscriber searching. But we do split on fresh publishes like wills and $SYS messages.
    if (_publish.splitTopic)
        splitTopic(this->publishData.topic, this->publishData.subtopics);

    packetType = PacketType::PUBLISH;
    this->publishData.qos = _publish.qos;
    first_byte = static_cast<char>(packetType) << 4;
    first_byte |= (_publish.qos << 1);
    first_byte |= (static_cast<char>(_publish.retain) & 0b00000001);

    writeUint16(publishData.topic.length());
    writeBytes(publishData.topic.c_str(), publishData.topic.length());

    if (publishData.qos)
    {
        // Reserve the space for the packet id, which will be assigned later.
        packet_id_pos = pos;
        char zero[2];
        writeBytes(zero, 2);
    }

    if (protocolVersion >= ProtocolVersion::Mqtt5)
    {
        // Step 1: make certain properties available as objects, because FlashMQ needs access to them for internal logic (only ACL checking at this point).
        if (_publish.splitTopic && _publish.hasUserProperties())
        {
            this->publishData.constructPropertyBuilder();
            this->publishData.propertyBuilder->setNewUserProperties(_publish.propertyBuilder->getUserProperties());
        }

        // Step 2: this line will make sure the whole byte array containing all properties as flat bytes is present in the 'bites' vector,
        // which is sent to the subscribers.
        writeProperties(_publish.propertyBuilder);
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
        writeByte(static_cast<uint8_t>(pubAck.reason_code));
    }
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
                throw ProtocolError("Packet signifies more than 5 bytes in variable length header. Invalid.");

            // This happens when you only don't have all the bytes that specify the remaining length.
            if (fixed_header_length > buf.usedBytes())
                return;

            encodedByte = buf.peakAhead(remaining_length_i++);
            packet_length += (encodedByte & 127) * multiplier;
            multiplier *= 128;
            if (multiplier > 128*128*128*128)
                throw ProtocolError("Malformed Remaining Length.");
        }
        while ((encodedByte & 128) != 0);
        packet_length += fixed_header_length;

        if (sender && !sender->getAuthenticated() && packet_length >= 1024*1024)
        {
            throw ProtocolError("An unauthenticated client sends a packet of 1 MB or bigger? Probably it's just random bytes.");
        }

        if (packet_length > ABSOLUTE_MAX_PACKET_SIZE)
        {
            throw ProtocolError("A client sends a packet claiming to be bigger than the maximum MQTT allows.");
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
    if (packetType == PacketType::Reserved)
        throw ProtocolError("Packet type 0 specified, which is reserved and invalid.");

    if (packetType != PacketType::CONNECT)
    {
        if (!sender->getAuthenticated())
        {
            logger->logf(LOG_WARNING, "Non-connect packet (%d) from non-authenticated client. Dropping packet.", packetType);
            return;
        }
    }

    if (packetType == PacketType::CONNECT)
        handleConnect();
    else if (packetType == PacketType::DISCONNECT)
        handleDisconnect();
    else if (packetType == PacketType::PINGREQ)
        sender->writePingResp();
    else if (packetType == PacketType::SUBSCRIBE)
        handleSubscribe();
    else if (packetType == PacketType::UNSUBSCRIBE)
        handleUnsubscribe();
    else if (packetType == PacketType::PUBLISH)
        handlePublish();
    else if (packetType == PacketType::PUBACK)
        handlePubAck();
    else if (packetType == PacketType::PUBREC)
        handlePubRec();
    else if (packetType == PacketType::PUBREL)
        handlePubRel();
    else if (packetType == PacketType::PUBCOMP)
        handlePubComp();
}

void MqttPacket::handleConnect()
{
    if (sender->hasConnectPacketSeen())
        throw ProtocolError("Client already sent a CONNECT.");

    std::shared_ptr<SubscriptionStore> subscriptionStore = sender->getThreadData()->getSubscriptionStore();

    uint16_t variable_header_length = readTwoBytesToUInt16();

    const Settings &settings = *ThreadGlobals::getSettings();

    if (variable_header_length == 4 || variable_header_length == 6)
    {
        char *c = readBytes(variable_header_length);
        std::string magic_marker(c, variable_header_length);

        char protocol_level = readByte();

        if (magic_marker == "MQTT")
        {
            if (protocol_level == 0x04)
                protocolVersion = ProtocolVersion::Mqtt311;
            if (protocol_level == 0x05)
                protocolVersion = ProtocolVersion::Mqtt5;
        }
        else if (magic_marker == "MQIsdp" && protocol_level == 0x03)
        {
            protocolVersion = ProtocolVersion::Mqtt31;
        }
        else
        {
            // The specs are unclear when to use the version 3 codes or version 5 codes.
            ProtocolVersion fuzzyProtocolVersion = protocol_level < 0x05 ? ProtocolVersion::Mqtt31 : ProtocolVersion::Mqtt5;

            ConnAck connAck(fuzzyProtocolVersion, ReasonCodes::UnsupportedProtocolVersion);
            MqttPacket response(connAck);
            sender->setReadyForDisconnect();
            sender->writeMqttPacket(response);
            logger->logf(LOG_ERR, "Rejecting because of invalid protocol version: %s", sender->repr().c_str());
            return;
        }

        char flagByte = readByte();
        bool reserved = !!(flagByte & 0b00000001);

        if (reserved)
            throw ProtocolError("Protocol demands reserved flag in CONNECT is 0");


        bool user_name_flag = !!(flagByte & 0b10000000);
        bool password_flag = !!(flagByte & 0b01000000);
        bool will_retain = !!(flagByte & 0b00100000);
        char will_qos = (flagByte & 0b00011000) >> 3;
        bool will_flag = !!(flagByte & 0b00000100);
        bool clean_start = !!(flagByte & 0b00000010);

        if (will_qos > 2)
            throw ProtocolError("Invalid QoS for will.");

        uint16_t keep_alive = readTwoBytesToUInt16();

        uint16_t max_qos_packets = settings.maxQosMsgPendingPerClient;
        uint32_t session_expire = settings.expireSessionsAfterSeconds > 0 ? settings.expireSessionsAfterSeconds : std::numeric_limits<uint32_t>::max();
        uint32_t max_packet_size = settings.maxPacketSize;
        uint16_t max_outgoing_topic_aliases = 0; // Default MUST BE 0, meaning server won't initiate aliases
        bool request_response_information = false;
        bool request_problem_information = false;

        if (protocolVersion == ProtocolVersion::Mqtt5)
        {
            const size_t proplen = decodeVariableByteIntAtPos();
            const size_t prop_end_at = pos + proplen;

            while (pos < prop_end_at)
            {
                const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readByte());

                switch (prop)
                {
                case Mqtt5Properties::SessionExpiryInterval:
                    session_expire = std::min<uint32_t>(readFourBytesToUint32(), session_expire);
                    break;
                case Mqtt5Properties::ReceiveMaximum:
                    max_qos_packets = std::min<int16_t>(readTwoBytesToUInt16(), max_qos_packets);
                    break;
                case Mqtt5Properties::MaximumPacketSize:
                    max_packet_size = std::min<uint32_t>(readFourBytesToUint32(), max_packet_size);
                    break;
                case Mqtt5Properties::TopicAliasMaximum:
                    max_outgoing_topic_aliases = std::min<uint16_t>(readTwoBytesToUInt16(), settings.maxOutgoingTopicAliasValue);
                    break;
                case Mqtt5Properties::RequestResponseInformation:
                    request_response_information = !!readByte();
                    break;
                case Mqtt5Properties::RequestProblemInformation:
                    request_problem_information = !!readByte();
                    break;
                case Mqtt5Properties::UserProperty:
                    readUserProperty();
                    break;
                case Mqtt5Properties::AuthenticationMethod:
                {
                    const uint16_t len = readTwoBytesToUInt16();
                    readBytes(len);
                    break;
                }
                case Mqtt5Properties::AuthenticationData:
                {
                    const uint16_t len = readTwoBytesToUInt16();
                    readBytes(len);
                    break;
                }
                default:
                    throw ProtocolError("Invalid connect property.");
                }
            }
        }

        uint16_t client_id_length = readTwoBytesToUInt16();
        std::string client_id(readBytes(client_id_length), client_id_length);

        std::string username;
        std::string password;

        Publish willpublish;
        willpublish.qos = will_qos;
        willpublish.retain = will_retain;

        if (will_flag)
        {
            if (protocolVersion == ProtocolVersion::Mqtt5)
            {
                willpublish.constructPropertyBuilder();

                const size_t proplen = decodeVariableByteIntAtPos();
                const size_t prop_end_at = pos + proplen;

                while (pos < prop_end_at)
                {
                    const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readByte());

                    switch (prop)
                    {
                    case Mqtt5Properties::WillDelayInterval:
                        willpublish.will_delay = readFourBytesToUint32();
                        willpublish.createdAt = std::chrono::steady_clock::now();
                        break;
                    case Mqtt5Properties::PayloadFormatIndicator:
                        willpublish.propertyBuilder->writePayloadFormatIndicator(readByte());
                        break;
                    case Mqtt5Properties::ContentType:
                    {
                        const uint16_t len = readTwoBytesToUInt16();
                        const std::string contentType(readBytes(len), len);
                        willpublish.propertyBuilder->writeContentType(contentType);
                        break;
                    }
                    case Mqtt5Properties::ResponseTopic:
                    {
                        const uint16_t len = readTwoBytesToUInt16();
                        const std::string responseTopic(readBytes(len), len);
                        willpublish.propertyBuilder->writeResponseTopic(responseTopic);
                        break;
                    }
                    case Mqtt5Properties::MessageExpiryInterval:
                    {
                        willpublish.createdAt = std::chrono::steady_clock::now();
                        uint32_t expiresAfter = readFourBytesToUint32();
                        willpublish.expiresAfter = std::chrono::seconds(expiresAfter);
                        break;
                    }
                    case Mqtt5Properties::CorrelationData:
                    {
                        const uint16_t len = readTwoBytesToUInt16();
                        const std::string correlationData(readBytes(len), len);
                        willpublish.propertyBuilder->writeCorrelationData(correlationData);
                        break;
                    }
                    case Mqtt5Properties::UserProperty:
                    {
                        const uint16_t lenKey = readTwoBytesToUInt16();
                        std::string userPropKey(readBytes(lenKey), lenKey);
                        const uint16_t lenVal = readTwoBytesToUInt16();
                        std::string userPropVal(readBytes(lenVal), lenVal);
                        willpublish.propertyBuilder->writeUserProperty(std::move(userPropKey), std::move(userPropVal));
                        break;
                    }
                    default:
                        throw ProtocolError("Invalid will property in connect.");
                    }
                }
            }

            uint16_t will_topic_length = readTwoBytesToUInt16();
            willpublish.topic = std::string(readBytes(will_topic_length), will_topic_length);

            uint16_t will_payload_length = readTwoBytesToUInt16();
            willpublish.payload = std::string(readBytes(will_payload_length), will_payload_length);
        }
        if (user_name_flag)
        {
            uint16_t user_name_length = readTwoBytesToUInt16();
            username = std::string(readBytes(user_name_length), user_name_length);

            if (username.empty())
                throw ProtocolError("Username flagged as present, but it's 0 bytes.");
        }
        if (password_flag)
        {
            uint16_t password_length = readTwoBytesToUInt16();
            password = std::string(readBytes(password_length), password_length);
        }

        // The specs don't really say what to do when client id not UTF8, so including here.
        if (!isValidUtf8(client_id) || !isValidUtf8(username) || !isValidUtf8(password) || !isValidUtf8(willpublish.topic))
        {
            ConnAck connAck(protocolVersion, ReasonCodes::BadUserNameOrPassword);
            MqttPacket response(connAck);
            sender->setReadyForDisconnect();
            sender->writeMqttPacket(response);
            logger->logf(LOG_ERR, "Client ID, username, password or will topic has invalid UTF8: ", client_id.c_str());
            return;
        }

        bool validClientId = true;

        // Check for wildcard chars in case the client_id ever appears in topics.
        if (!settings.allowUnsafeClientidChars && containsDangerousCharacters(client_id))
        {
            logger->logf(LOG_ERR, "ClientID '%s' has + or # in the id and 'allow_unsafe_clientid_chars' is false.", client_id.c_str());
            validClientId = false;
        }
        else if (!clean_start && client_id.empty())
        {
            logger->logf(LOG_ERR, "ClientID empty and clean start 0, which is incompatible");
            validClientId = false;
        }
        else if (protocolVersion < ProtocolVersion::Mqtt311 && client_id.empty())
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
        if (client_id.empty())
        {
            client_id = getSecureRandomString(23);
            clientIdGenerated = true;
        }

        sender->setClientProperties(protocolVersion, client_id, username, true, keep_alive, max_packet_size, max_outgoing_topic_aliases);

        if (will_flag)
            sender->setWill(std::move(willpublish));

        bool accessGranted = false;
        std::string denyLogMsg;

        Authentication &authentication = *ThreadGlobals::getAuth();

        if (!user_name_flag && settings.allowAnonymous)
        {
            accessGranted = true;
        }
        else if (!settings.allowUnsafeUsernameChars && containsDangerousCharacters(username))
        {
            denyLogMsg = formatString("Username '%s' has + or # in the id and 'allow_unsafe_username_chars' is false.", username.c_str());
            sender->setDisconnectReason("Invalid username character");
            accessGranted = false;
        }
        else if (authentication.unPwdCheck(username, password, getUserProperties()) == AuthResult::success)
        {
            accessGranted = true;
        }

        if (accessGranted)
        {
            bool sessionPresent = protocolVersion >= ProtocolVersion::Mqtt311 && !clean_start && subscriptionStore->sessionPresent(client_id);

            sender->setAuthenticated(true);
            ConnAck connAck(protocolVersion, ReasonCodes::Success, sessionPresent);

            if (protocolVersion >= ProtocolVersion::Mqtt5)
            {
                connAck.propertyBuilder = std::make_shared<Mqtt5PropertyBuilder>();
                connAck.propertyBuilder->writeSessionExpiry(session_expire);
                connAck.propertyBuilder->writeReceiveMax(max_qos_packets);
                connAck.propertyBuilder->writeRetainAvailable(1);
                connAck.propertyBuilder->writeMaxPacketSize(max_packet_size);
                if (clientIdGenerated)
                    connAck.propertyBuilder->writeAssignedClientId(client_id);
                connAck.propertyBuilder->writeMaxTopicAliases(settings.maxIncomingTopicAliasValue);
                connAck.propertyBuilder->writeWildcardSubscriptionAvailable(1);
                connAck.propertyBuilder->writeSubscriptionIdentifiersAvailable(0);
                connAck.propertyBuilder->writeSharedSubscriptionAvailable(0);
            }

            MqttPacket response(connAck);
            sender->writeMqttPacket(response);
            logger->logf(LOG_NOTICE, "Client '%s' logged in successfully", sender->repr().c_str());

            subscriptionStore->registerClientAndKickExistingOne(sender, clean_start, max_qos_packets, session_expire);
        }
        else
        {
            ConnAck connDeny(protocolVersion, ReasonCodes::NotAuthorized, false);
            MqttPacket response(connDeny);
            sender->setDisconnectReason("Access denied");
            sender->setReadyForDisconnect();
            sender->writeMqttPacket(response);
            if (!denyLogMsg.empty())
                logger->logf(LOG_NOTICE, denyLogMsg.c_str());
            else
                logger->logf(LOG_NOTICE, "User '%s' access denied", username.c_str());
        }
    }
    else
    {
        throw ProtocolError("Invalid variable header length. Garbage?");
    }
}

void MqttPacket::handleDisconnect()
{
    logger->logf(LOG_NOTICE, "Client '%s' cleanly disconnecting", sender->repr().c_str());
    sender->setDisconnectReason("MQTT Disconnect received.");
    sender->markAsDisconnecting();
    sender->clearWill();
    sender->getThreadData()->removeClientQueued(sender);
}

void MqttPacket::handleSubscribe()
{
    const char firstByteFirstNibble = (first_byte & 0x0F);

    if (firstByteFirstNibble != 2)
        throw ProtocolError("First LSB of first byte is wrong value for subscribe packet.");

    const uint16_t packet_id = readTwoBytesToUInt16();

    if (packet_id == 0)
    {
        throw ProtocolError("Packet ID 0 when subscribing is invalid."); // [MQTT-2.3.1-1]
    }

    if (protocolVersion == ProtocolVersion::Mqtt5)
    {
        const size_t proplen = decodeVariableByteIntAtPos();
        const size_t prop_end_at = pos + proplen;

        while (pos < prop_end_at)
        {
            const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readByte());

            switch (prop)
            {
            case Mqtt5Properties::SubscriptionIdentifier:
                decodeVariableByteIntAtPos();
                break;
            case Mqtt5Properties::UserProperty:
                readUserProperty();
                break;
            default:
                throw ProtocolError("Invalid subscribe property.");
            }
        }
    }

    Authentication &authentication = *ThreadGlobals::getAuth();

    std::list<ReasonCodes> subs_reponse_codes;
    while (remainingAfterPos() > 0)
    {
        uint16_t topicLength = readTwoBytesToUInt16();
        std::string topic(readBytes(topicLength), topicLength);

        if (topic.empty() || !isValidUtf8(topic))
            throw ProtocolError("Subscribe topic not valid UTF-8.");

        if (!isValidSubscribePath(topic))
            throw ProtocolError(formatString("Invalid subscribe path: %s", topic.c_str()));

        uint8_t qos = readByte();

        if (qos > 2)
            throw ProtocolError("QoS is greater than 2, and/or reserved bytes in QoS field are not 0.");

        std::vector<std::string> subtopics;
        splitTopic(topic, subtopics);
        if (authentication.aclCheck(sender->getClientId(), sender->getUsername(), topic, subtopics, AclAccess::subscribe, qos, false, getUserProperties()) == AuthResult::success)
        {
            logger->logf(LOG_SUBSCRIBE, "Client '%s' subscribed to '%s' QoS %d", sender->repr().c_str(), topic.c_str(), qos);
            sender->getThreadData()->getSubscriptionStore()->addSubscription(sender, topic, subtopics, qos);
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
        throw ProtocolError("No topics specified to subscribe to.");
    }

    SubAck subAck(this->protocolVersion, packet_id, subs_reponse_codes);
    MqttPacket response(subAck);
    sender->writeMqttPacket(response);
}

void MqttPacket::handleUnsubscribe()
{
    const char firstByteFirstNibble = (first_byte & 0x0F);

    if (firstByteFirstNibble != 2)
        throw ProtocolError("First LSB of first byte is wrong value for subscribe packet.");

    const uint16_t packet_id = readTwoBytesToUInt16();

    if (packet_id == 0)
    {
        throw ProtocolError("Packet ID 0 when unsubscribing is invalid."); // [MQTT-2.3.1-1]
    }

    if (protocolVersion == ProtocolVersion::Mqtt5)
    {
        const size_t proplen = decodeVariableByteIntAtPos();
        const size_t prop_end_at = pos + proplen;

        while (pos < prop_end_at)
        {
            const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readByte());

            switch (prop)
            {
            case Mqtt5Properties::UserProperty:
                readUserProperty();
                break;
            default:
                throw ProtocolError("Invalid unsubscribe property.");
            }
        }
    }

    int numberOfUnsubs = 0;

    while (remainingAfterPos() > 0)
    {
        numberOfUnsubs++;

        uint16_t topicLength = readTwoBytesToUInt16();
        std::string topic(readBytes(topicLength), topicLength);

        if (topic.empty() || !isValidUtf8(topic))
            throw ProtocolError("Subscribe topic not valid UTF-8.");

        sender->getThreadData()->getSubscriptionStore()->removeSubscription(sender, topic);
        logger->logf(LOG_UNSUBSCRIBE, "Client '%s' unsubscribed from '%s'", sender->repr().c_str(), topic.c_str());
    }

    // MQTT-3.10.3-2
    if (numberOfUnsubs == 0)
    {
        throw ProtocolError("No topics specified to unsubscribe to.");
    }

    UnsubAck unsubAck(this->sender->getProtocolVersion(), packet_id, numberOfUnsubs);
    MqttPacket response(unsubAck);
    sender->writeMqttPacket(response);
}

void MqttPacket::handlePublish()
{
    const uint16_t variable_header_length = readTwoBytesToUInt16();

    bool retain = (first_byte & 0b00000001);
    bool dup = !!(first_byte & 0b00001000);
    char qos = (first_byte & 0b00000110) >> 1;

    if (qos > 2)
        throw ProtocolError("QoS 3 is a protocol violation.");
    this->publishData.qos = qos;

    if (qos == 0 && dup)
        throw ProtocolError("Duplicate flag is set for QoS 0 packet. This is illegal.");

    publishData.topic = std::string(readBytes(variable_header_length), variable_header_length);

    ReasonCodes ackCode = ReasonCodes::Success;

    if (qos)
    {
        packet_id_pos = pos;
        packet_id = readTwoBytesToUInt16();

        if (packet_id == 0)
        {
            throw ProtocolError("Packet ID 0 when publishing is invalid."); // [MQTT-2.3.1-1]
        }
    }

    if (this->protocolVersion >= ProtocolVersion::Mqtt5 )
    {
        const size_t proplen = decodeVariableByteIntAtPos();
        const size_t prop_end_at = pos + proplen;

        while (pos < prop_end_at)
        {
            const Mqtt5Properties prop = static_cast<Mqtt5Properties>(readByte());

            switch (prop)
            {
            case Mqtt5Properties::PayloadFormatIndicator:
                publishData.constructPropertyBuilder();
                publishData.propertyBuilder->writePayloadFormatIndicator(readByte());
                break;
            case Mqtt5Properties::MessageExpiryInterval:
                publishData.createdAt = std::chrono::steady_clock::now();
                publishData.expiresAfter = std::chrono::seconds(readFourBytesToUint32());
                break;
            case Mqtt5Properties::TopicAlias:
            {
                const uint16_t alias_id = readTwoBytesToUInt16();
                this->hasTopicAlias = true;

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
                const uint16_t len = readTwoBytesToUInt16();
                const std::string responseTopic(readBytes(len), len);
                publishData.propertyBuilder->writeResponseTopic(responseTopic);
                break;
            }
            case Mqtt5Properties::CorrelationData:
            {
                publishData.constructPropertyBuilder();
                const uint16_t len = readTwoBytesToUInt16();
                const std::string correlationData(readBytes(len), len);
                publishData.propertyBuilder->writeCorrelationData(correlationData);
                break;
            }
            case Mqtt5Properties::UserProperty:
            {
                publishData.constructPropertyBuilder();
                const uint16_t lenKey = readTwoBytesToUInt16();
                std::string userPropKey(readBytes(lenKey), lenKey);
                const uint16_t lenVal = readTwoBytesToUInt16();
                std::string userPropVal(readBytes(lenVal), lenVal);
                publishData.propertyBuilder->writeUserProperty(std::move(userPropKey), std::move(userPropVal));
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
                throw ProtocolError("Invalid property in publish.");
            }
        }
    }

    if (publishData.topic.empty())
        throw ProtocolError("Empty publish topic");

    if (!isValidUtf8(publishData.topic, true))
    {
        const std::string err = formatString("Client '%s' published a message with invalid UTF8 or $/+/# in it. Dropping.", sender->repr().c_str());
        logger->logf(LOG_WARNING, err.c_str());
        throw ProtocolError(err);
    }

#ifndef NDEBUG
    logger->logf(LOG_DEBUG, "Publish received, topic '%s'. QoS=%d. Retain=%d, dup=%d", publishData.topic.c_str(), qos, retain, dup);
#endif

    sender->getThreadData()->incrementReceivedMessageCount();

    payloadLen = remainingAfterPos();
    payloadStart = pos;

    Authentication &authentication = *ThreadGlobals::getAuth();

    // Working with a local copy because the subscribing action will modify this->packet_id. See the PublishCopyFactory.
    const uint16_t _packet_id = this->packet_id;

    if (qos == 2 && sender->getSession()->incomingQoS2MessageIdInTransit(_packet_id))
    {
        ackCode = ReasonCodes::PacketIdentifierInUse;
    }
    else
    {
        // Doing this before the authentication on purpose, so when the publish is not allowed, the QoS control packets are allowed and can finish.
        if (qos == 2)
            sender->getSession()->addIncomingQoS2MessageId(_packet_id);

        splitTopic(publishData.topic, publishData.subtopics);

        if (authentication.aclCheck(sender->getClientId(), sender->getUsername(), publishData.topic, publishData.subtopics, AclAccess::write, qos, retain, getUserProperties()) == AuthResult::success)
        {
            if (retain)
            {
                std::string payload(readBytes(payloadLen), payloadLen);
                sender->getThreadData()->getSubscriptionStore()->setRetainedMessage(publishData.topic, publishData.subtopics, payload, qos);
            }

            // Set dup flag to 0, because that must not be propagated [MQTT-3.3.1-3].
            // Existing subscribers don't get retain=1. [MQTT-3.3.1-9]
            bites[0] &= 0b11110110;
            first_byte = bites[0];

            PublishCopyFactory factory(this);
            sender->getThreadData()->getSubscriptionStore()->queuePacketAtSubscribers(factory);
        }
        else
        {
            ackCode = ReasonCodes::NotAuthorized;
        }
    }

#ifndef NDEBUG
    // Protection against using the altered packet id.
    this->packet_id = 0;
#endif

    if (qos > 0)
    {
        const PacketType responseType = qos == 1 ? PacketType::PUBACK : PacketType::PUBREC;
        PubResponse pubAck(this->protocolVersion, responseType, ackCode, _packet_id);
        MqttPacket response(pubAck);
        sender->writeMqttPacket(response);
    }
}

void MqttPacket::handlePubAck()
{
    uint16_t packet_id = readTwoBytesToUInt16();
    sender->getSession()->clearQosMessage(packet_id);
}

/**
 * @brief MqttPacket::handlePubRec handles QoS 2 'publish received' packets. The publisher receives these.
 */
void MqttPacket::handlePubRec()
{
    const uint16_t packet_id = readTwoBytesToUInt16();
    sender->getSession()->clearQosMessage(packet_id);
    sender->getSession()->addOutgoingQoS2MessageId(packet_id);

    PubResponse pubRel(this->protocolVersion, PacketType::PUBREL, ReasonCodes::Success, packet_id);
    MqttPacket response(pubRel);
    sender->writeMqttPacket(response);
}

/**
 * @brief MqttPacket::handlePubRel handles QoS 2 'publish release'. The publisher sends these.
 */
void MqttPacket::handlePubRel()
{
    // MQTT-3.6.1-1, but why do we care, and only care for certain control packets?
    if (first_byte & 0b1101)
        throw ProtocolError("PUBREL first byte LSB must be 0010.");

    const uint16_t packet_id = readTwoBytesToUInt16();
    sender->getSession()->removeIncomingQoS2MessageId(packet_id);

    PubResponse pubcomp(this->protocolVersion, PacketType::PUBCOMP, ReasonCodes::Success, packet_id);
    MqttPacket response(pubcomp);
    sender->writeMqttPacket(response);
}

/**
 * @brief MqttPacket::handlePubComp handles QoS 2 'publish complete'. The publisher receives these.
 */
void MqttPacket::handlePubComp()
{
    const uint16_t packet_id = readTwoBytesToUInt16();
    sender->getSession()->removeOutgoingQoS2MessageId(packet_id);
}

void MqttPacket::calculateRemainingLength()
{
    assert(fixed_header_length == 0); // because you're not supposed to call this on packet that we already know the length of.
    this->remainingLength = bites.size();
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

size_t MqttPacket::getSizeIncludingNonPresentHeader() const
{
    size_t total = bites.size();

    if (fixed_header_length == 0)
    {
        total++;
        total += remainingLength.getLen();
    }

    return total;
}

void MqttPacket::setQos(const char new_qos)
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

/**
 * @brief MqttPacket::getSubtopics returns a pointer to the parsed subtopics. Use with care!
 * @return a pointer to a vector of subtopics that will be overwritten the next packet!
 */
const std::vector<std::string> &MqttPacket::getSubtopics() const
{
    return this->publishData.subtopics;
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

char *MqttPacket::readBytes(size_t length)
{
    if (pos + length > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.");

    char *b = &bites[pos];
    pos += length;
    return b;
}

char MqttPacket::readByte()
{
    if (pos + 1 > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.");

    char b = bites[pos++];
    return b;
}

void MqttPacket::writeByte(char b)
{
    if (pos + 1 > bites.size())
        throw ProtocolError("Exceeding packet size");

    bites[pos++] = b;
}

void MqttPacket::writeUint16(uint16_t x)
{
    if (pos + 2 > bites.size())
        throw ProtocolError("Exceeding packet size");

    const uint8_t a = static_cast<uint8_t>(x >> 8);
    const uint8_t b = static_cast<uint8_t>(x);

    bites[pos++] = a;
    bites[pos++] = b;
}

void MqttPacket::writeBytes(const char *b, size_t len)
{
    if (pos + len > bites.size())
        throw ProtocolError("Exceeding packet size");

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

uint16_t MqttPacket::readTwoBytesToUInt16()
{
    if (pos + 2 > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.");

    uint8_t a = bites[pos];
    uint8_t b = bites[pos+1];
    uint16_t i = a << 8 | b;
    pos += 2;
    return i;
}

uint32_t MqttPacket::readFourBytesToUint32()
{
    if (pos + 4 > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.");

    const uint8_t a = bites[pos++];
    const uint8_t b = bites[pos++];
    const uint8_t c = bites[pos++];
    const uint8_t d = bites[pos++];
    uint32_t i = (a << 24) | (b << 16) | (c << 8) | d;
    return i;
}

size_t MqttPacket::remainingAfterPos()
{
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
            throw ProtocolError("Variable byte int length goes out of packet. Corrupt.");

        encodedByte = bites[pos++];
        value += (encodedByte & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128*128*128*128)
            throw ProtocolError("Malformed Remaining Length.");
    }
    while ((encodedByte & 128) != 0);

    return value;
}

void MqttPacket::readUserProperty()
{
    this->publishData.constructPropertyBuilder();

    const uint16_t len = readTwoBytesToUInt16();
    std::string key(readBytes(len), len);
    const uint16_t len2 = readTwoBytesToUInt16();
    std::string value(readBytes(len2), len2);

    this->publishData.propertyBuilder->writeUserProperty(std::move(key), std::move(value));
}

const std::vector<std::pair<std::string, std::string>> *MqttPacket::getUserProperties() const
{
    if (this->publishData.propertyBuilder)
        return this->publishData.propertyBuilder->getUserProperties().get();

    return nullptr;
}

bool MqttPacket::getRetain() const
{
    return (first_byte & 0b00000001);
}

/**
 * @brief MqttPacket::setRetain set the retain bit in the first byte. I think I only need this in tests, because existing subscribers don't get retain=1,
 * so handlePublish() clears it. But I needed it to be set in testing.
 *
 * Publishing of the retained messages goes through the MqttPacket(Publish) constructor, hence this setRetain() isn't necessary for that.
 */
void MqttPacket::setRetain()
{
#ifndef TESTING
    assert(false);
#endif

    first_byte |= 0b00000001;

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

    if (protocolVersion <= ProtocolVersion::Mqtt311 || !publishData.propertyBuilder)
        return false;

    if (publishData.createdAt.time_since_epoch().count() == 0 || this->hasTopicAlias) // TODO: better
    {
        return true;
    }

    return false;
}

void MqttPacket::readIntoBuf(CirBuf &buf) const
{
    assert(packetType != PacketType::PUBLISH || (first_byte & 0b00000110) >> 1 == publishData.qos);

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











