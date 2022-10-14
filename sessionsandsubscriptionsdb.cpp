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
#include "threadglobals.h"
#include "utils.h"

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
    PersistenceFile::openWrite(MAGIC_STRING_SESSION_FILE_V3);
}

void SessionsAndSubscriptionsDB::openRead()
{
    PersistenceFile::openRead();

    if (detectedVersionString == MAGIC_STRING_SESSION_FILE_V1)
        readVersion = ReadVersion::v1;
    else if (detectedVersionString == MAGIC_STRING_SESSION_FILE_V2)
        readVersion = ReadVersion::v2;
    else if (detectedVersionString == MAGIC_STRING_SESSION_FILE_V3)
        readVersion = ReadVersion::v3;
    else
        throw std::runtime_error("Unknown file version.");
}

SessionsAndSubscriptionsResult SessionsAndSubscriptionsDB::readDataV3()
{
    const Settings *settings = ThreadGlobals::getSettings();

    SessionsAndSubscriptionsResult result;

    while (!feof(f))
    {
        bool eofFound = false;

        const int64_t fileSavedAt = readInt64(eofFound);
        if (eofFound)
            continue;

        const int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const int64_t persistence_state_age = fileSavedAt > now_epoch ? 0 : now_epoch - fileSavedAt;

        logger->logf(LOG_DEBUG, "Session file was saved at %ld. That's %ld seconds ago.", fileSavedAt, persistence_state_age);

        const uint32_t nrOfSessions = readUint32(eofFound);

        if (eofFound)
            continue;

        std::vector<char> reserved(RESERVED_SPACE_SESSIONS_DB_V2);
        CirBuf cirbuf(1024);

        std::shared_ptr<ThreadData> dummyThreadData; // which thread am I going get/use here?
        std::shared_ptr<Client> dummyClient(new Client(0, dummyThreadData, nullptr, false, nullptr, settings, false));
        dummyClient->setClientProperties(ProtocolVersion::Mqtt5, "Dummyforloadingqueuedqos", "nobody", true, 60);

        for (uint32_t i = 0; i < nrOfSessions; i++)
        {
            readCheck(buf.data(), 1, RESERVED_SPACE_SESSIONS_DB_V2, f);

            uint32_t usernameLength = readUint32(eofFound);
            readCheck(buf.data(), 1, usernameLength, f);
            std::string username(buf.data(), usernameLength);

            uint32_t clientIdLength = readUint32(eofFound);
            readCheck(buf.data(), 1, clientIdLength, f);
            std::string clientId(buf.data(), clientIdLength);

            std::shared_ptr<Session> ses = std::make_shared<Session>();
            result.sessions.push_back(ses);
            ses->username = username;
            ses->client_id = clientId;

            logger->logf(LOG_DEBUG, "Loading session '%s'.", ses->getClientId().c_str());

            const uint32_t nrOfQueuedQoSPackets = readUint32(eofFound);
            for (uint32_t i = 0; i < nrOfQueuedQoSPackets; i++)
            {
                const uint16_t fixed_header_length = readUint16(eofFound);
                const uint16_t id = readUint16(eofFound);
                const uint32_t originalPubAge = readUint32(eofFound);
                const uint32_t packlen = readUint32(eofFound);
                const std::string sender_clientid = readString(eofFound);
                const std::string sender_username = readString(eofFound);

                assert(id > 0);

                cirbuf.reset();
                cirbuf.ensureFreeSpace(packlen + 32);

                readCheck(cirbuf.headPtr(), 1, packlen, f);
                cirbuf.advanceHead(packlen);
                MqttPacket pack(cirbuf, packlen, fixed_header_length, dummyClient);

                pack.parsePublishData();
                Publish pub(pack.getPublishData());

                pub.client_id = sender_clientid;
                pub.username = sender_username;

                const uint32_t newPubAge = persistence_state_age + originalPubAge;
                pub.createdAt = timepointFromAge(newPubAge);

                logger->logf(LOG_DEBUG, "Loaded QoS %d message for topic '%s' for session '%s'.", pub.qos, pub.topic.c_str(), ses->getClientId().c_str());
                ses->qosPacketQueue.queuePublish(std::move(pub), id);
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

            const uint32_t originalSessionExpiryInterval = readUint32(eofFound);
            const uint32_t compensatedSessionExpiry = persistence_state_age > originalSessionExpiryInterval ? 0 : originalSessionExpiryInterval - persistence_state_age;
            const uint32_t sessionExpiryInterval = std::min<uint32_t>(compensatedSessionExpiry, settings->getExpireSessionAfterSeconds());

            // We will set the session expiry interval as it would have had time continued. If a connection picks up session, it will update
            // it with a more relevant value.
            // The protocol version 5 is just dummy, to get the behavior I want.
            ses->setSessionProperties(0xFFFF, sessionExpiryInterval, 0, ProtocolVersion::Mqtt5);

            const uint16_t hasWill = readUint16(eofFound);

            if (hasWill)
            {
                const uint16_t fixed_header_length = readUint16(eofFound);
                const uint32_t originalWillDelay = readUint32(eofFound);
                const uint32_t originalWillQueueAge = readUint32(eofFound);
                const uint32_t newWillDelayAfterMaybeAlreadyBeingQueued = originalWillQueueAge < originalWillDelay ? originalWillDelay - originalWillQueueAge : 0;
                const uint32_t packlen = readUint32(eofFound);
                const std::string sender_clientid = readString(eofFound);
                const std::string sender_username = readString(eofFound);

                const uint32_t stateAgecompensatedWillDelay =
                        persistence_state_age > newWillDelayAfterMaybeAlreadyBeingQueued ? 0 : newWillDelayAfterMaybeAlreadyBeingQueued - persistence_state_age;

                cirbuf.reset();
                cirbuf.ensureFreeSpace(packlen + 32);

                readCheck(cirbuf.headPtr(), 1, packlen, f);
                cirbuf.advanceHead(packlen);
                MqttPacket publishpack(cirbuf, packlen, fixed_header_length, dummyClient);
                publishpack.parsePublishData();
                WillPublish willPublish = publishpack.getPublishData();
                willPublish.will_delay = stateAgecompensatedWillDelay;

                willPublish.client_id = sender_clientid;
                willPublish.username = sender_username;

                ses->setWill(std::move(willPublish));
            }
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

void SessionsAndSubscriptionsDB::saveData(const std::vector<std::shared_ptr<Session>> &sessions, const std::unordered_map<std::string, std::list<SubscriptionForSerializing>> &subscriptions)
{
    if (!f)
        return;

    char reserved[RESERVED_SPACE_SESSIONS_DB_V2];
    std::memset(reserved, 0, RESERVED_SPACE_SESSIONS_DB_V2);

    const int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    logger->logf(LOG_DEBUG, "Saving current time stamp %ld", now_epoch);
    writeInt64(now_epoch);

    std::vector<std::shared_ptr<Session>> sessionsToSave;
    // Sessions created with clean session need to be destroyed when disconnecting, so no point in saving them.
    std::copy_if(sessions.begin(), sessions.end(), std::back_inserter(sessionsToSave), [](const std::shared_ptr<Session> &ses) {
        return !ses->destroyOnDisconnect;
    });

    writeUint32(sessionsToSave.size());

    CirBuf cirbuf(1024);

    for (const std::shared_ptr<Session> &_ses : sessionsToSave)
    {
        // Takes care of locking, and working on the snapshot/copy prevents doing disk IO under lock.
        std::unique_ptr<Session> ses = _ses->getCopy();

        logger->logf(LOG_DEBUG, "Saving session '%s'.", ses->getClientId().c_str());

        writeRowHeader();

        writeCheck(reserved, 1, RESERVED_SPACE_SESSIONS_DB_V2, f);

        writeString(ses->username);
        writeString(ses->client_id);

        const size_t qosPacketsExpected = ses->qosPacketQueue.size();
        size_t qosPacketsCounted = 0;
        writeUint32(qosPacketsExpected);

        std::shared_ptr<QueuedPublish> qp;
        while ((qp = ses->qosPacketQueue.next()))
        {
            QueuedPublish &p = *qp;

            qosPacketsCounted++;

            Publish &pub = p.getPublish();

            assert(!pub.skipTopic);
            assert(pub.topicAlias == 0);

            logger->logf(LOG_DEBUG, "Saving QoS %d message for topic '%s'.", pub.qos, pub.topic.c_str());

            MqttPacket pack(ProtocolVersion::Mqtt5, pub);
            pack.setPacketId(p.getPacketId());
            const uint32_t packSize = pack.getSizeIncludingNonPresentHeader();
            cirbuf.reset();
            cirbuf.ensureFreeSpace(packSize + 32);
            pack.readIntoBuf(cirbuf);

            const uint32_t pubAge = ageFromTimePoint(pub.getCreatedAt());

            writeUint16(pack.getFixedHeaderLength());
            writeUint16(p.getPacketId());
            writeUint32(pubAge);
            writeUint32(packSize);
            writeString(pub.client_id);
            writeString(pub.username);
            writeCheck(cirbuf.tailPtr(), 1, cirbuf.usedBytes(), f);
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

        writeUint32(ses->getCurrentSessionExpiryInterval());

        const bool hasWillThatShouldSurviveRestart = ses->getWill().operator bool() && ses->getWill()->will_delay > 0;
        writeUint16(static_cast<uint16_t>(hasWillThatShouldSurviveRestart));

        if (hasWillThatShouldSurviveRestart)
        {
            WillPublish &will = *ses->getWill().get();
            MqttPacket willpacket(ProtocolVersion::Mqtt5, will);

            // Dummy, to please the parser on reading.
            if (will.qos > 0)
                willpacket.setPacketId(666);

            const uint32_t packSize = willpacket.getSizeIncludingNonPresentHeader();
            cirbuf.reset();
            cirbuf.ensureFreeSpace(packSize + 32);
            willpacket.readIntoBuf(cirbuf);

            writeUint16(willpacket.getFixedHeaderLength());
            writeUint32(will.will_delay);
            writeUint32(will.getQueuedAtAge());
            writeUint32(packSize);
            writeString(will.client_id);
            writeString(will.username);
            writeCheck(cirbuf.tailPtr(), 1, cirbuf.usedBytes(), f);
        }
    }

    writeUint32(subscriptions.size());

    for (auto &pair : subscriptions)
    {
        const std::string &topic = pair.first;
        const std::list<SubscriptionForSerializing> &subscriptions = pair.second;

        logger->logf(LOG_DEBUG, "Writing subscriptions to topic '%s'.", topic.c_str());

        writeString(topic);

        writeUint32(subscriptions.size());

        for (const SubscriptionForSerializing &subscription : subscriptions)
        {
            logger->logf(LOG_DEBUG, "Saving session '%s' subscription to '%s' QoS %d.", subscription.clientId.c_str(), topic.c_str(), subscription.qos);

            writeString(subscription.clientId);
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
        logger->logf(LOG_WARNING, "File '%s' is version 1, an internal development version that was never finalized. Not reading.", getFilePath().c_str());
    if (readVersion == ReadVersion::v2)
        logger->logf(LOG_WARNING, "File '%s' is version 2, an internal development version that was never finalized. Not reading.", getFilePath().c_str());
    if (readVersion == ReadVersion::v3)
        return readDataV3();

    return defaultResult;
}
