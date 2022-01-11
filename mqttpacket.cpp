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
#include "threadauth.h"

RemainingLength::RemainingLength()
{
    memset(bytes, 0, 4);
}

// constructor for parsing incoming packets
MqttPacket::MqttPacket(CirBuf &buf, size_t packet_len, size_t fixed_header_length, std::shared_ptr<Client> &sender) :
    bites(packet_len),
    fixed_header_length(fixed_header_length),
    sender(sender)
{
    assert(packet_len > 0);
    buf.read(bites.data(), packet_len);

    first_byte = bites[0];
    unsigned char _packetType = (first_byte & 0xF0) >> 4;
    packetType = (PacketType)_packetType;
    pos += fixed_header_length;
}

/**
 * @brief MqttPacket::getCopy (using default copy constructor and resetting some selected fields) is easier than using the copy constructor
 *        publically, because then I have to keep maintaining a functioning copy constructor for each new field I add.
 * @return a shared pointer because that's typically how we need it; we only need to copy it if we pass it around as shared resource.
 */
std::shared_ptr<MqttPacket> MqttPacket::getCopy() const
{
    std::shared_ptr<MqttPacket> copyPacket(new MqttPacket(*this));
    copyPacket->sender.reset();
    return copyPacket;
}

// This constructor cheats and doesn't use calculateRemainingLength, because it's always the same. It allocates enough space in the vector.
MqttPacket::MqttPacket(const ConnAck &connAck) :
    bites(connAck.getLengthWithoutFixedHeader() + 2)
{
    fixed_header_length = 2;
    packetType = PacketType::CONNACK;
    char first_byte = static_cast<char>(packetType) << 4;
    writeByte(first_byte);
    writeByte(2); // length is always 2.
    writeByte(connAck.session_present & 0b00000001); // all connect-ack flags are 0, except session-present. [MQTT-3.2.2.1]
    writeByte(static_cast<char>(connAck.return_code));
}

MqttPacket::MqttPacket(const SubAck &subAck) :
    bites(subAck.getLengthWithoutFixedHeader())
{
    packetType = PacketType::SUBACK;
    first_byte = static_cast<char>(packetType) << 4;
    writeByte((subAck.packet_id & 0xFF00) >> 8);
    writeByte(subAck.packet_id & 0x00FF);

    std::vector<char> returnList;
    for (SubAckReturnCodes code : subAck.responses)
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
    writeByte((unsubAck.packet_id & 0xFF00) >> 8);
    writeByte(unsubAck.packet_id & 0x00FF);
    calculateRemainingLength();
}

MqttPacket::MqttPacket(const Publish &publish) :
    bites(publish.getLengthWithoutFixedHeader())
{
    if (publish.topic.length() > 0xFFFF)
    {
        throw ProtocolError("Topic path too long.");
    }

    this->topic = publish.topic;
    splitTopic(this->topic, subtopics);

    packetType = PacketType::PUBLISH;
    this->qos = publish.qos;
    first_byte = static_cast<char>(packetType) << 4;
    first_byte |= (publish.qos << 1);
    first_byte |= (static_cast<char>(publish.retain) & 0b00000001);

    char topicLenMSB = (topic.length() & 0xFF00) >> 8;
    char topicLenLSB = topic.length() & 0x00FF;
    writeByte(topicLenMSB);
    writeByte(topicLenLSB);
    writeBytes(topic.c_str(), topic.length());

    if (publish.qos)
    {
        // Reserve the space for the packet id, which will be assigned later.
        packet_id_pos = pos;
        char zero[2];
        writeBytes(zero, 2);
    }

    payloadStart = pos;
    payloadLen = publish.payload.length();

    writeBytes(publish.payload.c_str(), publish.payload.length());
    calculateRemainingLength();
}

/**
 * @brief MqttPacket::pubCommonConstruct is common code for constructors for all those empty pub control packets (ack, rec(eived), rel(ease), comp(lete)).
 * @param packet_id
 *
 * This functions cheats a bit and doesn't use calculateRemainingLength, because it's always 2. Be sure to allocate enough room in the vector when
 * you use this function (add 2 to the length).
 */
void MqttPacket::pubCommonConstruct(const uint16_t packet_id, PacketType packetType, uint8_t firstByteDefaultBits)
{
    assert(firstByteDefaultBits <= 0xF);

    fixed_header_length = 2;
    first_byte = (static_cast<uint8_t>(packetType) << 4) | firstByteDefaultBits;
    writeByte(first_byte);
    writeByte(2); // length is always 2.
    uint8_t packetIdMSB = (packet_id & 0xFF00) >> 8;
    uint8_t packetIdLSB = (packet_id & 0x00FF);
    packet_id_pos = pos;
    writeByte(packetIdMSB);
    writeByte(packetIdLSB);
}

MqttPacket::MqttPacket(const PubAck &pubAck) :
    bites(pubAck.getLengthWithoutFixedHeader() + 2)
{
    pubCommonConstruct(pubAck.packet_id, PacketType::PUBACK);
}

MqttPacket::MqttPacket(const PubRec &pubRec) :
    bites(pubRec.getLengthWithoutFixedHeader() + 2)
{
    pubCommonConstruct(pubRec.packet_id, PacketType::PUBREC);
}

MqttPacket::MqttPacket(const PubComp &pubComp) :
    bites(pubComp.getLengthWithoutFixedHeader() + 2)
{
    pubCommonConstruct(pubComp.packet_id, PacketType::PUBCOMP);
}

MqttPacket::MqttPacket(const PubRel &pubRel) :
    bites(pubRel.getLengthWithoutFixedHeader() + 2)
{
    pubCommonConstruct(pubRel.packet_id, PacketType::PUBREL, 0b0010);
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

    const Settings &settings = sender->getThreadData()->settingsLocalCopy;

    if (variable_header_length == 4 || variable_header_length == 6)
    {
        char *c = readBytes(variable_header_length);
        std::string magic_marker(c, variable_header_length);

        char protocol_level = readByte();

        if (magic_marker == "MQTT" && protocol_level == 0x04)
        {
            protocolVersion = ProtocolVersion::Mqtt311;
        }
        else if (magic_marker == "MQIsdp" && protocol_level == 0x03)
        {
            protocolVersion = ProtocolVersion::Mqtt31;
        }
        else
        {
            ConnAck connAck(ConnAckReturnCodes::UnacceptableProtocolVersion);
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
        bool clean_session = !!(flagByte & 0b00000010);

        if (will_qos > 2)
            throw ProtocolError("Invalid QoS for will.");

        uint16_t keep_alive = readTwoBytesToUInt16();

        uint16_t client_id_length = readTwoBytesToUInt16();
        std::string client_id(readBytes(client_id_length), client_id_length);

        std::string username;
        std::string password;
        std::string will_topic;
        std::string will_payload;

        if (will_flag)
        {
            uint16_t will_topic_length = readTwoBytesToUInt16();
            will_topic = std::string(readBytes(will_topic_length), will_topic_length);

            uint16_t will_payload_length = readTwoBytesToUInt16();
            will_payload = std::string(readBytes(will_payload_length), will_payload_length);
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
        if (!isValidUtf8(client_id) || !isValidUtf8(username) || !isValidUtf8(password) || !isValidUtf8(will_topic))
        {
            ConnAck connAck(ConnAckReturnCodes::MalformedUsernameOrPassword);
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
        else if (!clean_session && client_id.empty())
        {
            logger->logf(LOG_ERR, "ClientID empty and clean session 0, which is incompatible");
            validClientId = false;
        }
        else if (protocolVersion < ProtocolVersion::Mqtt311 && client_id.empty())
        {
            logger->logf(LOG_ERR, "Empty clientID. Connect with protocol 3.1.1 or higher to have one generated securely.");
            validClientId = false;
        }

        if (!validClientId)
        {
            ConnAck connAck(ConnAckReturnCodes::ClientIdRejected);
            MqttPacket response(connAck);
            sender->setDisconnectReason("Invalid clientID");
            sender->setReadyForDisconnect();
            sender->writeMqttPacket(response);
            return;
        }

        if (client_id.empty())
        {
            client_id = getSecureRandomString(23);
        }

        sender->setClientProperties(protocolVersion, client_id, username, true, keep_alive, clean_session);
        sender->setWill(will_topic, will_payload, will_retain, will_qos);

        bool accessGranted = false;
        std::string denyLogMsg;

        Authentication &authentication = *ThreadAuth::getAuth();

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
        else if (authentication.unPwdCheck(username, password) == AuthResult::success)
        {
            accessGranted = true;
        }

        if (accessGranted)
        {
            bool sessionPresent = protocolVersion >= ProtocolVersion::Mqtt311 && !clean_session && subscriptionStore->sessionPresent(client_id);

            sender->setAuthenticated(true);
            ConnAck connAck(ConnAckReturnCodes::Accepted, sessionPresent);
            MqttPacket response(connAck);
            sender->writeMqttPacket(response);
            logger->logf(LOG_NOTICE, "Client '%s' logged in successfully", sender->repr().c_str());

            subscriptionStore->registerClientAndKickExistingOne(sender);
        }
        else
        {
            ConnAck connDeny(ConnAckReturnCodes::NotAuthorized, false);
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

    uint16_t packet_id = readTwoBytesToUInt16();

    if (packet_id == 0)
    {
        throw ProtocolError("Packet ID 0 when subscribing is invalid."); // [MQTT-2.3.1-1]
    }

    Authentication &authentication = *ThreadAuth::getAuth();

    std::list<char> subs_reponse_codes;
    while (remainingAfterPos() > 0)
    {
        uint16_t topicLength = readTwoBytesToUInt16();
        std::string topic(readBytes(topicLength), topicLength);

        if (topic.empty() || !isValidUtf8(topic))
            throw ProtocolError("Subscribe topic not valid UTF-8.");

        if (!isValidSubscribePath(topic))
            throw ProtocolError(formatString("Invalid subscribe path: %s", topic.c_str()));

        char qos = readByte();

        if (qos > 2)
            throw ProtocolError("QoS is greater than 2, and/or reserved bytes in QoS field are not 0.");

        splitTopic(topic, subtopics);
        if (authentication.aclCheck(sender->getClientId(), sender->getUsername(), topic, subtopics, AclAccess::subscribe, qos, false) == AuthResult::success)
        {
            logger->logf(LOG_SUBSCRIBE, "Client '%s' subscribed to '%s' QoS %d", sender->repr().c_str(), topic.c_str(), qos);
            sender->getThreadData()->getSubscriptionStore()->addSubscription(sender, topic, subtopics, qos);
            subs_reponse_codes.push_back(qos);
        }
        else
        {
            logger->logf(LOG_SUBSCRIBE, "Client '%s' subscribe to '%s' denied or failed.", sender->repr().c_str(), topic.c_str());

            // We can't not send an ack, because if there are multiple subscribes, you send fewer acks back, losing sync.
            char return_code = qos;
            if (sender->getProtocolVersion() >= ProtocolVersion::Mqtt311)
                return_code = static_cast<char>(SubAckReturnCodes::Fail);
            subs_reponse_codes.push_back(return_code);
        }
    }

    // MQTT-3.8.3-3
    if (subs_reponse_codes.empty())
    {
        throw ProtocolError("No topics specified to subscribe to.");
    }

    SubAck subAck(packet_id, subs_reponse_codes);
    MqttPacket response(subAck);
    sender->writeMqttPacket(response);
}

void MqttPacket::handleUnsubscribe()
{
    const char firstByteFirstNibble = (first_byte & 0x0F);

    if (firstByteFirstNibble != 2)
        throw ProtocolError("First LSB of first byte is wrong value for subscribe packet.");

    uint16_t packet_id = readTwoBytesToUInt16();

    if (packet_id == 0)
    {
        throw ProtocolError("Packet ID 0 when unsubscribing is invalid."); // [MQTT-2.3.1-1]
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

    UnsubAck unsubAck(packet_id);
    MqttPacket response(unsubAck);
    sender->writeMqttPacket(response);
}

void MqttPacket::handlePublish()
{
    uint16_t variable_header_length = readTwoBytesToUInt16();

    if (variable_header_length == 0)
        throw ProtocolError("Empty publish topic");

    bool retain = (first_byte & 0b00000001);
    bool dup = !!(first_byte & 0b00001000);
    char qos = (first_byte & 0b00000110) >> 1;

    if (qos > 2)
        throw ProtocolError("QoS 3 is a protocol violation.");
    this->qos = qos;

    if (qos == 0 && dup)
        throw ProtocolError("Duplicate flag is set for QoS 0 packet. This is illegal.");

    topic = std::string(readBytes(variable_header_length), variable_header_length);
    splitTopic(topic, subtopics);

    if (!isValidUtf8(topic, true))
    {
        logger->logf(LOG_WARNING, "Client '%s' published a message with invalid UTF8 or $/+/# in it. Dropping.", sender->repr().c_str());
        return;
    }

#ifndef NDEBUG
    logger->logf(LOG_DEBUG, "Publish received, topic '%s'. QoS=%d. Retain=%d, dup=%d", topic.c_str(), qos, retain, dup);
#endif

    sender->getThreadData()->incrementReceivedMessageCount();

    if (qos)
    {
        packet_id_pos = pos;
        packet_id = readTwoBytesToUInt16();

        if (packet_id == 0)
        {
            throw ProtocolError("Packet ID 0 when publishing is invalid."); // [MQTT-2.3.1-1]
        }

        if (qos == 1)
        {
            PubAck pubAck(packet_id);
            MqttPacket response(pubAck);
            sender->writeMqttPacket(response);
        }
        else
        {
            PubRec pubRec(packet_id);
            MqttPacket response(pubRec);
            sender->writeMqttPacket(response);

            if (sender->getSession()->incomingQoS2MessageIdInTransit(packet_id))
            {
                return;
            }
            else
            {
                // Doing this before the authentication on purpose, so when the publish is not allowed, the QoS control packets are allowed and can finish.
                sender->getSession()->addIncomingQoS2MessageId(packet_id);
            }
        }
    }

    payloadLen = remainingAfterPos();
    payloadStart = pos;

    Authentication &authentication = *ThreadAuth::getAuth();
    if (authentication.aclCheck(sender->getClientId(), sender->getUsername(), topic, subtopics, AclAccess::write, qos, retain) == AuthResult::success)
    {
        if (retain)
        {
            std::string payload(readBytes(payloadLen), payloadLen);
            sender->getThreadData()->getSubscriptionStore()->setRetainedMessage(topic, subtopics, payload, qos);
        }

        // Set dup flag to 0, because that must not be propagated [MQTT-3.3.1-3].
        // Existing subscribers don't get retain=1. [MQTT-3.3.1-9]
        bites[0] &= 0b11110110;
        first_byte = bites[0];

        // For the existing clients, we can just write the same packet back out, with our small alterations.
        sender->getThreadData()->getSubscriptionStore()->queuePacketAtSubscribers(subtopics, *this);
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

    PubRel pubRel(packet_id);
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

    PubComp pubcomp(packet_id);
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

    size_t x = bites.size();

    do
    {
        if (remainingLength.len > 4)
            throw std::runtime_error("Calculated remaining length is longer than 4 bytes.");

        char encodedByte = x % 128;
        x = x / 128;
        if (x > 0)
            encodedByte = encodedByte | 128;
        remainingLength.bytes[remainingLength.len++] = encodedByte;
    }
    while(x > 0);
}

RemainingLength MqttPacket::getRemainingLength() const
{
    assert(remainingLength.len > 0);
    return remainingLength;
}

void MqttPacket::setPacketId(uint16_t packet_id)
{
    assert(fixed_header_length == 0 || first_byte == bites[0]);
    assert(packet_id_pos > 0);
    assert(packetType == PacketType::PUBLISH);
    assert(qos > 0);

    this->packet_id = packet_id;

    pos = packet_id_pos;

    char topicLenMSB = (packet_id & 0xFF00) >> 8;
    char topicLenLSB = (packet_id & 0x00FF);
    writeByte(topicLenMSB);
    writeByte(topicLenLSB);
}

uint16_t MqttPacket::getPacketId() const
{
    assert(qos > 0);
    return packet_id;
}

// If I read the specs correctly, the DUP flag is merely for show. It doesn't control anything?
void MqttPacket::setDuplicate()
{
    assert(packetType == PacketType::PUBLISH);
    assert(qos > 0);
    assert(fixed_header_length == 0 || first_byte == bites[0]);

    first_byte |= 0b00001000;

    if (fixed_header_length > 0)
    {
        pos = 0;
        writeByte(first_byte);
    }
}

size_t MqttPacket::getTotalMemoryFootprint()
{
    return bites.size() + sizeof(MqttPacket);
}

/**
 * @brief MqttPacket::getPayloadCopy takes part of the vector of bytes and returns it as a string.
 * @return
 *
 * It's necessary sometimes, but it's against FlashMQ's concept of not parsing the payload. Normally, you can just write out
 * the whole byte array that is a packet to subscribers. No need to copy and such.
 *
 * I created it for saving QoS packages in the db file.
 */
std::string MqttPacket::getPayloadCopy()
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
        total += remainingLength.len;
    }

    return total;
}

const std::string &MqttPacket::getTopic() const
{
    return this->topic;
}

/**
 * @brief MqttPacket::getSubtopics returns a pointer to the parsed subtopics. Use with care!
 * @return a pointer to a vector of subtopics that will be overwritten the next packet!
 */
const std::vector<std::string> &MqttPacket::getSubtopics() const
{
    return this->subtopics;
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

char MqttPacket::getFirstByte() const
{
    return first_byte;
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

void MqttPacket::writeBytes(const char *b, size_t len)
{
    if (pos + len > bites.size())
        throw ProtocolError("Exceeding packet size");

    memcpy(&bites[pos], b, len);
    pos += len;
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

size_t MqttPacket::remainingAfterPos()
{
    return bites.size() - pos;
}











