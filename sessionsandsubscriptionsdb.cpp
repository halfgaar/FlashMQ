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

#include "sessionsandsubscriptionsdb.h"
#include "mqttpacket.h"

#include "cassert"

SubscriptionForSerializing::SubscriptionForSerializing(const std::string &clientId, char qos) :
    clientId(clientId),
    qos(qos)
{

}

SubscriptionForSerializing::SubscriptionForSerializing(const std::string &&clientId, char qos) :
    clientId(clientId),
    qos(qos)
{

}

SessionsAndSubscriptionsDB::SessionsAndSubscriptionsDB(const std::string &filePath) : PersistenceFile(filePath)
{

}

void SessionsAndSubscriptionsDB::openWrite()
{
    PersistenceFile::openWrite(MAGIC_STRING_SESSION_FILE_V1);
}

void SessionsAndSubscriptionsDB::openRead()
{
    PersistenceFile::openRead();

    if (detectedVersionString == MAGIC_STRING_SESSION_FILE_V1)
        readVersion = ReadVersion::v1;
    else
        throw std::runtime_error("Unknown file version.");
}

SessionsAndSubscriptionsResult SessionsAndSubscriptionsDB::readDataV1()
{
    SessionsAndSubscriptionsResult result;

    while (!feof(f))
    {
        bool eofFound = false;

        const int64_t programStartAge = readInt64(eofFound);
        if (eofFound)
            continue;

        logger->logf(LOG_DEBUG, "Setting first app start time to timestamp %ld", programStartAge);
        Session::setProgramStartedAtUnixTimestamp(programStartAge);

        const uint32_t nrOfSessions = readUint32(eofFound);

        if (eofFound)
            continue;

        std::vector<char> reserved(RESERVED_SPACE_SESSIONS_DB_V1);

        for (uint32_t i = 0; i < nrOfSessions; i++)
        {
            readCheck(buf.data(), 1, RESERVED_SPACE_SESSIONS_DB_V1, f);

            uint32_t usernameLength = readUint32(eofFound);
            readCheck(buf.data(), 1, usernameLength, f);
            std::string username(buf.data(), usernameLength);

            uint32_t clientIdLength = readUint32(eofFound);
            readCheck(buf.data(), 1, clientIdLength, f);
            std::string clientId(buf.data(), clientIdLength);

            std::shared_ptr<Session> ses(new Session());
            result.sessions.push_back(ses);
            ses->username = username;
            ses->client_id = clientId;

            logger->logf(LOG_DEBUG, "Loading session '%s'.", ses->getClientId().c_str());

            const uint32_t nrOfQueuedQoSPackets = readUint32(eofFound);
            for (uint32_t i = 0; i < nrOfQueuedQoSPackets; i++)
            {
                const uint16_t id = readUint16(eofFound);
                const uint32_t topicSize = readUint32(eofFound);
                const uint32_t payloadSize = readUint32(eofFound);

                assert(id > 0);

                readCheck(buf.data(), 1, 1, f);
                const unsigned char qos = buf[0];

                readCheck(buf.data(), 1, topicSize, f);
                const std::string topic(buf.data(), topicSize);

                makeSureBufSize(payloadSize);
                readCheck(buf.data(), 1, payloadSize, f);
                const std::string payload(buf.data(), payloadSize);

                Publish pub(topic, payload, qos);
                logger->logf(LOG_DEBUG, "Loaded QoS %d message for topic '%s'.", pub.qos, pub.topic.c_str());
                ses->qosPacketQueue.queuePacket(pub, id);
            }

            const uint32_t nrOfIncomingPacketIds = readUint32(eofFound);
            for (uint32_t i = 0; i < nrOfIncomingPacketIds; i++)
            {
                uint16_t id = readUint16(eofFound);
                assert(id > 0);
                logger->logf(LOG_DEBUG, "Loaded incomming QoS2 message id %d.", id);
                ses->incomingQoS2MessageIds.insert(id);
            }

            const uint32_t nrOfOutgoingPacketIds = readUint32(eofFound);
            for (uint32_t i = 0; i < nrOfOutgoingPacketIds; i++)
            {
                uint16_t id = readUint16(eofFound);
                assert(id > 0);
                logger->logf(LOG_DEBUG, "Loaded outgoing QoS2 message id %d.", id);
                ses->outgoingQoS2MessageIds.insert(id);
            }

            const uint16_t nextPacketId = readUint16(eofFound);
            logger->logf(LOG_DEBUG, "Loaded next packetid %d.", ses->nextPacketId);
            ses->nextPacketId = nextPacketId;

            int64_t sessionAge = readInt64(eofFound);
            logger->logf(LOG_DEBUG, "Loaded session age: %ld ms.", sessionAge);
            ses->setSessionTouch(sessionAge);
        }

        const uint32_t nrOfSubscriptions = readUint32(eofFound);
        for (uint32_t i = 0; i < nrOfSubscriptions; i++)
        {
            const uint32_t topicLength = readUint32(eofFound);
            readCheck(buf.data(), 1, topicLength, f);
            const std::string topic(buf.data(), topicLength);

            logger->logf(LOG_DEBUG, "Loading subscriptions to topic '%s'.", topic.c_str());

            const uint32_t nrOfClientIds = readUint32(eofFound);

            for (uint32_t i = 0; i < nrOfClientIds; i++)
            {
                const uint32_t clientIdLength = readUint32(eofFound);
                readCheck(buf.data(), 1, clientIdLength, f);
                const std::string clientId(buf.data(), clientIdLength);

                char qos;
                readCheck(&qos, 1, 1, f);

                logger->logf(LOG_DEBUG, "Saving session '%s' subscription to '%s' QoS %d.", clientId.c_str(), topic.c_str(), qos);

                SubscriptionForSerializing sub(std::move(clientId), qos);
                result.subscriptions[topic].push_back(std::move(sub));
            }

        }
    }

    return result;
}

void SessionsAndSubscriptionsDB::writeRowHeader()
{

}

void SessionsAndSubscriptionsDB::saveData(const std::list<std::unique_ptr<Session>> &sessions, const std::unordered_map<std::string, std::list<SubscriptionForSerializing>> &subscriptions)
{
    if (!f)
        return;

    char reserved[RESERVED_SPACE_SESSIONS_DB_V1];
    std::memset(reserved, 0, RESERVED_SPACE_SESSIONS_DB_V1);

    const int64_t start_stamp = Session::getProgramStartedAtUnixTimestamp();
    logger->logf(LOG_DEBUG, "Saving program first start time stamp as %ld", start_stamp);
    writeInt64(start_stamp);

    writeUint32(sessions.size());

    for (const std::unique_ptr<Session> &ses : sessions)
    {
        logger->logf(LOG_DEBUG, "Saving session '%s'.", ses->getClientId().c_str());

        writeRowHeader();

        writeCheck(reserved, 1, RESERVED_SPACE_SESSIONS_DB_V1, f);

        writeUint32(ses->username.length());
        writeCheck(ses->username.c_str(), 1, ses->username.length(), f);

        writeUint32(ses->client_id.length());
        writeCheck(ses->client_id.c_str(), 1, ses->client_id.length(), f);

        const size_t qosPacketsExpected = ses->qosPacketQueue.size();
        size_t qosPacketsCounted = 0;
        writeUint32(qosPacketsExpected);

        for (const std::shared_ptr<MqttPacket> &p: ses->qosPacketQueue)
        {
            logger->logf(LOG_DEBUG, "Saving QoS %d message for topic '%s'.", p->getQos(), p->getTopic().c_str());

            qosPacketsCounted++;

            writeUint16(p->getPacketId());

            writeUint32(p->getTopic().length());
            std::string payload = p->getPayloadCopy();
            writeUint32(payload.size());

            const char qos = p->getQos();
            writeCheck(&qos, 1, 1, f);

            writeCheck(p->getTopic().c_str(), 1, p->getTopic().length(), f);
            writeCheck(payload.c_str(), 1, payload.length(), f);
        }

        assert(qosPacketsExpected == qosPacketsCounted);

        writeUint32(ses->incomingQoS2MessageIds.size());
        for (uint16_t id : ses->incomingQoS2MessageIds)
        {
            logger->logf(LOG_DEBUG, "Writing incomming QoS2 message id %d.", id);
            writeUint16(id);
        }

        writeUint32(ses->outgoingQoS2MessageIds.size());
        for (uint16_t id : ses->outgoingQoS2MessageIds)
        {
            logger->logf(LOG_DEBUG, "Writing outgoing QoS2 message id %d.", id);
            writeUint16(id);
        }

        logger->logf(LOG_DEBUG, "Writing next packetid %d.", ses->nextPacketId);
        writeUint16(ses->nextPacketId);

        const int64_t sInMs = ses->getSessionRelativeAgeInMs();
        logger->logf(LOG_DEBUG, "Writing session age: %ld ms.", sInMs);
        writeInt64(sInMs);
    }

    writeUint32(subscriptions.size());

    for (auto &pair : subscriptions)
    {
        const std::string &topic = pair.first;
        const std::list<SubscriptionForSerializing> &subscriptions = pair.second;

        logger->logf(LOG_DEBUG, "Writing subscriptions to topic '%s'.", topic.c_str());

        writeUint32(topic.size());
        writeCheck(topic.c_str(), 1, topic.size(), f);

        writeUint32(subscriptions.size());

        for (const SubscriptionForSerializing &subscription : subscriptions)
        {
            logger->logf(LOG_DEBUG, "Saving session '%s' subscription to '%s' QoS %d.", subscription.clientId.c_str(), topic.c_str(), subscription.qos);

            writeUint32(subscription.clientId.size());
            writeCheck(subscription.clientId.c_str(), 1, subscription.clientId.size(), f);
            writeCheck(&subscription.qos, 1, 1, f);
        }
    }

    fflush(f);
}

SessionsAndSubscriptionsResult SessionsAndSubscriptionsDB::readData()
{
    SessionsAndSubscriptionsResult defaultResult;

    if (!f)
        return defaultResult;

    if (readVersion == ReadVersion::v1)
        return readDataV1();

    return defaultResult;
}
