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

#include "threadlocalutils.h"

thread_local Utils utils;

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

// This is easier than using the copy constructor publically, because then I have to keep maintaining a functioning copy constructor.
// Returning shared pointer because that's typically how we need it; we only need to copy it if we pass it around as shared resource.
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
    this->subtopics = utils.splitTopic(this->topic);

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

    writeBytes(publish.payload.c_str(), publish.payload.length());
    calculateRemainingLength();
}

// This constructor cheats and doesn't use calculateRemainingLength, because it's always the same. It allocates enough space in the vector.
MqttPacket::MqttPacket(const PubAck &pubAck) :
    bites(pubAck.getLengthWithoutFixedHeader() + 2)
{
    fixed_header_length = 2; // This is the cheat part mentioned above. We're not calculating it dynamically.
    packetType = PacketType::PUBACK;
    first_byte = static_cast<char>(packetType) << 4;
    writeByte(first_byte);
    writeByte(2); // length is always 2.
    char topicLenMSB = (pubAck.packet_id & 0xFF00) >> 8;
    char topicLenLSB = (pubAck.packet_id & 0x00FF);
    writeByte(topicLenMSB);
    writeByte(topicLenLSB);
}

void MqttPacket::handle()
{
    if (packetType == PacketType::Reserved)
        throw ProtocolError("Packet type 0 specified, which is reserved and invalid.");

    if (packetType != PacketType::CONNECT)
    {
        if (!sender->getAuthenticated())
        {
            throw ProtocolError("Non-connect packet from non-authenticated client.");
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
        if (!utils.isValidUtf8(client_id) || !utils.isValidUtf8(username) || !utils.isValidUtf8(password) || !utils.isValidUtf8(will_topic))
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

        if (!settings.allowUnsafeUsernameChars && containsDangerousCharacters(username))
        {
            denyLogMsg = formatString("Username '%s' has + or # in the id and 'allow_unsafe_username_chars' is false.", username.c_str());
            sender->setDisconnectReason("Invalid username character");
            accessGranted = false;
        }
        else if (sender->getThreadData()->authentication.unPwdCheck(username, password) == AuthResult::success)
        {
            accessGranted = true;
        }

        if (accessGranted)
        {
            bool sessionPresent = protocolVersion >= ProtocolVersion::Mqtt311 && !clean_session && subscriptionStore->sessionPresent(client_id);
            subscriptionStore->registerClientAndKickExistingOne(sender);

            sender->setAuthenticated(true);
            ConnAck connAck(ConnAckReturnCodes::Accepted, sessionPresent);
            MqttPacket response(connAck);
            sender->writeMqttPacket(response);
            logger->logf(LOG_NOTICE, "Client '%s' logged in successfully", sender->repr().c_str());
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
    sender->getThreadData()->removeClient(sender);
}

void MqttPacket::handleSubscribe()
{
    const char firstByteFirstNibble = (first_byte & 0x0F);

    if (firstByteFirstNibble != 2)
        throw ProtocolError("First LSB of first byte is wrong value for subscribe packet.");

    uint16_t packet_id = readTwoBytesToUInt16();

    std::list<char> subs_reponse_codes;
    while (remainingAfterPos() > 0)
    {
        uint16_t topicLength = readTwoBytesToUInt16();
        std::string topic(readBytes(topicLength), topicLength);

        if (topic.empty() || !utils.isValidUtf8(topic))
            throw ProtocolError("Subscribe topic not valid UTF-8.");

        if (!isValidSubscribePath(topic))
            throw ProtocolError(formatString("Invalid subscribe path: %s", topic.c_str()));

        char qos = readByte();

        if (qos > 2)
            throw ProtocolError("QoS is greater than 2, and/or reserved bytes in QoS field are not 0.");

        logger->logf(LOG_SUBSCRIBE, "Client '%s' subscribed to '%s'", sender->repr().c_str(), topic.c_str());
        sender->getThreadData()->getSubscriptionStore()->addSubscription(sender, topic, qos);
        subs_reponse_codes.push_back(qos);
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

    while (remainingAfterPos() > 0)
    {
        uint16_t topicLength = readTwoBytesToUInt16();
        std::string topic(readBytes(topicLength), topicLength);

        if (topic.empty() || !utils.isValidUtf8(topic))
            throw ProtocolError("Subscribe topic not valid UTF-8.");

        sender->getThreadData()->getSubscriptionStore()->removeSubscription(sender, topic);
        logger->logf(LOG_UNSUBSCRIBE, "Client '%s' unsubscribed from '%s'", sender->repr().c_str(), topic.c_str());
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

    if (qos == 3)
        throw ProtocolError("QoS 3 is a protocol violation.");
    this->qos = qos;

    if (qos == 0 && dup)
        throw ProtocolError("Duplicate flag is set for QoS 0 packet. This is illegal.");

    topic = std::string(readBytes(variable_header_length), variable_header_length);
    subtopics = utils.splitTopic(topic);

    if (!utils.isValidUtf8(topic, true))
    {
        logger->logf(LOG_WARNING, "Client '%s' published a message with invalid UTF8 or +/# in it. Dropping.", sender->repr().c_str());
        return;
    }

    if (qos)
    {
        if (qos > 1)
            throw ProtocolError("Qos > 1 not implemented.");
        packet_id_pos = pos;
        uint16_t packet_id = readTwoBytesToUInt16();

        // Clear the packet ID from this packet, because each new publish must get a new one. It's more of a debug precaution.
        pos -= 2;
        char zero[2]; zero[0] = 0; zero[1] = 0;
        writeBytes(zero, 2);

        PubAck pubAck(packet_id);
        MqttPacket response(pubAck);
        sender->writeMqttPacket(response);
    }

    if (sender->getThreadData()->authentication.aclCheck(sender->getClientId(), sender->getUsername(), topic, *subtopics, AclAccess::write) == AuthResult::success)
    {
        if (retain)
        {
            size_t payload_length = remainingAfterPos();
            std::string payload(readBytes(payload_length), payload_length);

            sender->getThreadData()->getSubscriptionStore()->setRetainedMessage(topic, payload, qos);
        }

        // Set dup flag to 0, because that must not be propagated [MQTT-3.3.1-3].
        // Existing subscribers don't get retain=1. [MQTT-3.3.1-9]
        bites[0] &= 0b11110110;
        first_byte = bites[0];

        // For the existing clients, we can just write the same packet back out, with our small alterations.
        sender->getThreadData()->getSubscriptionStore()->queuePacketAtSubscribers(*subtopics, *this);
    }
}

void MqttPacket::handlePubAck()
{
    uint16_t packet_id = readTwoBytesToUInt16();
    sender->getSession()->clearQosMessage(packet_id);
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

    pos = packet_id_pos;

    char topicLenMSB = (packet_id & 0xFF00) >> 8;
    char topicLenLSB = (packet_id & 0x00FF);
    writeByte(topicLenMSB);
    writeByte(topicLenLSB);
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

const std::vector<std::string> *MqttPacket::getSubtopics() const
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

    uint16_t i = bites[pos] << 8 | bites[pos+1];
    pos += 2;
    return i;
}

size_t MqttPacket::remainingAfterPos()
{
    return bites.size() - pos;
}











