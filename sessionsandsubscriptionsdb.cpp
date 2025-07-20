/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "sessionsandsubscriptionsdb.h"

#include <inttypes.h>
#include <optional>

#include "mqttpacket.h"
#include "threadglobals.h"
#include "utils.h"
#include "client.h"
#include "session.h"
#include "settings.h"

#include <cassert>

SubscriptionForSerializing::SubscriptionForSerializing(const std::string &clientId, uint8_t qos, bool noLocal, bool retainAsPublished,
                                                       uint32_t subscriptionidentifier) :
    clientId(clientId),
    qos(qos),
    noLocal(noLocal),
    retainAsPublished(retainAsPublished),
    subscriptionidentifier(subscriptionidentifier)
{

}

SubscriptionForSerializing::SubscriptionForSerializing(const std::string &clientId, uint8_t qos, bool noLocal, bool retainAsPublished,
                                                       uint32_t subscriptionidentifier, const std::string &shareName) :
    clientId(clientId),
    qos(qos),
    shareName(shareName),
    noLocal(noLocal),
    retainAsPublished(retainAsPublished),
    subscriptionidentifier(subscriptionidentifier)
{

}

SubscriptionForSerializing::SubscriptionForSerializing(const std::string &&clientId, uint8_t qos, bool noLocal, bool retainAsPublished,
                                                       uint32_t subscriptionidentifier) :
    clientId(std::move(clientId)),
    qos(qos),
    noLocal(noLocal),
    retainAsPublished(retainAsPublished),
    subscriptionidentifier(subscriptionidentifier)
{

}

SubscriptionForSerializing::SubscriptionForSerializing(const std::string &&clientId, SubscriptionOptionsByte options,
                                                       uint32_t subscriptionidentifier, const std::string &shareName) :
    clientId(std::move(clientId)),
    qos(options.getQos()),
    shareName(shareName),
    noLocal(options.getNoLocal()),
    retainAsPublished(options.getRetainAsPublished()),
    subscriptionidentifier(subscriptionidentifier)
{

}

SubscriptionOptionsByte SubscriptionForSerializing::getSubscriptionOptions() const
{
    return SubscriptionOptionsByte(qos, noLocal, retainAsPublished, RetainHandling::SendRetainedMessagesAtSubscribe);
}

SessionsAndSubscriptionsDB::SessionsAndSubscriptionsDB(const std::string &filePath) : PersistenceFile(filePath)
{

}

void SessionsAndSubscriptionsDB::openWrite()
{
    PersistenceFile::openWrite(MAGIC_STRING_SESSION_FILE_V8);
}

void SessionsAndSubscriptionsDB::openRead()
{
    const std::string current_magic_string(MAGIC_STRING_SESSION_FILE_V8);

    PersistenceFile::openRead(current_magic_string);

    if (detectedVersionString == MAGIC_STRING_SESSION_FILE_V1)
        readVersion = ReadVersion::v1;
    else if (detectedVersionString == MAGIC_STRING_SESSION_FILE_V2)
        readVersion = ReadVersion::v2;
    else if (detectedVersionString == MAGIC_STRING_SESSION_FILE_V3)
        readVersion = ReadVersion::v3;
    else if (detectedVersionString == MAGIC_STRING_SESSION_FILE_V4)
        readVersion = ReadVersion::v4;
    else if (detectedVersionString == MAGIC_STRING_SESSION_FILE_V5)
        readVersion = ReadVersion::v5;
    else if (detectedVersionString == MAGIC_STRING_SESSION_FILE_V6)
        readVersion = ReadVersion::v6;
    else if (detectedVersionString == MAGIC_STRING_SESSION_FILE_V7)
        readVersion = ReadVersion::v7;
    else if (detectedVersionString == current_magic_string)
        readVersion = ReadVersion::v8;
    else
        throw std::runtime_error("Unknown file version.");
}

SessionsAndSubscriptionsResult SessionsAndSubscriptionsDB::readDataV3V4V5V6V7()
{
    const Settings &settings = *ThreadGlobals::getSettings();

    SessionsAndSubscriptionsResult result;

    while (!feof(f))
    {
        bool eofFound = false;

        const int64_t fileSavedAt = readInt64(eofFound);
        if (eofFound)
            continue;

        const int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const int64_t persistence_state_age = fileSavedAt > now_epoch ? 0 : now_epoch - fileSavedAt;

        logger->log(LOG_DEBUG) << "Session file was saved at " << fileSavedAt << ". That's " << persistence_state_age << " seconds ago.";

        const uint32_t nrOfSessions = readUint32(eofFound);

        if (eofFound)
            continue;

        std::vector<char> reserved(RESERVED_SPACE_SESSIONS_DB_V2);
        CirBuf cirbuf(1024);

        std::shared_ptr<ThreadData> dummyThreadData; // which thread am I going get/use here?
        std::shared_ptr<Client> dummyClient(std::make_shared<Client>(ClientType::Normal, 0, dummyThreadData, FmqSsl(), ConnectionProtocol::Mqtt, false, nullptr, settings, false));
        dummyClient->setClientProperties(ProtocolVersion::Mqtt5, "Dummyforloadingqueuedqos", {}, "nobody", true, 60);

        for (uint32_t i = 0; i < nrOfSessions; i++)
        {
            readCheck(buf.data(), 1, RESERVED_SPACE_SESSIONS_DB_V2, f);

            std::string username = readString(eofFound);
            std::string clientId = readString(eofFound);

            std::optional<std::string> fmq_client_group_id;
            if (readVersion >= ReadVersion::v8)
                fmq_client_group_id = readOptionalString(eofFound);

            std::shared_ptr<Session> ses = std::make_shared<Session>(clientId, username, fmq_client_group_id);
            result.sessions.push_back(ses);

            logger->logf(LOG_DEBUG, "Loading session '%s'.", ses->getClientId().c_str());

            {
                MutexLocked<Session::QoSData> qos_locked = ses->qos.lock();

                const uint32_t nrOfQueuedQoSPackets = readUint32(eofFound);
                for (uint32_t i = 0; i < nrOfQueuedQoSPackets; i++)
                {
                    const uint16_t fixed_header_length = readUint16(eofFound);
                    const uint16_t id = readUint16(eofFound);
                    const uint32_t originalPubAge = readUint32(eofFound);
                    const uint32_t packlen = readUint32(eofFound);
                    const std::string sender_clientid = readString(eofFound);
                    const std::string sender_username = readString(eofFound);

                    std::optional<std::string> topic_override;
                    if (readVersion >= ReadVersion::v7)
                        topic_override = readOptionalString(eofFound);

                    assert(id > 0);

                    cirbuf.reset();
                    cirbuf.ensureFreeSpace(packlen + 32);

                    readCheck(cirbuf.headPtr(), 1, packlen, f);
                    cirbuf.advanceHead(packlen);
                    MqttPacket pack(cirbuf.readToVector(packlen), fixed_header_length, dummyClient);

                    pack.parsePublishData(dummyClient);
                    Publish pub(pack.getPublishData());

                    pub.client_id = sender_clientid;
                    pub.username = sender_username;

                    const uint32_t newPubAge = persistence_state_age + originalPubAge;
                    if (pub.expireInfo)
                        pub.expireInfo->createdAt = timepointFromAge(newPubAge);

                    logger->logf(LOG_DEBUG, "Loaded QoS %d message for topic '%s' for session '%s'.", pub.qos, pub.topic.c_str(), ses->getClientId().c_str());
                    qos_locked->qosPacketQueue.queuePublish(std::move(pub), id, topic_override);
                }

                const uint32_t nrOfIncomingPacketIds = readUint32(eofFound);
                for (uint32_t i = 0; i < nrOfIncomingPacketIds; i++)
                {
                    uint16_t id = readUint16(eofFound);
                    assert(id > 0);
                    logger->logf(LOG_DEBUG, "Loaded incomming QoS2 message id %d.", id);
                    qos_locked->incomingQoS2MessageIds.insert(id);
                }

                const uint32_t nrOfOutgoingPacketIds = readUint32(eofFound);
                for (uint32_t i = 0; i < nrOfOutgoingPacketIds; i++)
                {
                    uint16_t id = readUint16(eofFound);
                    assert(id > 0);
                    logger->logf(LOG_DEBUG, "Loaded outgoing QoS2 message id %d.", id);
                    qos_locked->outgoingQoS2MessageIds.insert(id);
                }

                const uint16_t nextPacketId = readUint16(eofFound);
                logger->logf(LOG_DEBUG, "Loaded next packetid %d.", qos_locked->nextPacketId);
                qos_locked->nextPacketId = nextPacketId;
            }

            const uint32_t originalSessionExpiryInterval = readUint32(eofFound);
            const uint32_t compensatedSessionExpiry = persistence_state_age > originalSessionExpiryInterval ? 0 : originalSessionExpiryInterval - persistence_state_age;
            const uint32_t sessionExpiryInterval = std::min<uint32_t>(compensatedSessionExpiry, settings.getExpireSessionAfterSeconds());

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
                MqttPacket publishpack(cirbuf.readToVector(packlen), fixed_header_length, dummyClient);
                publishpack.parsePublishData(dummyClient);
                WillPublish willPublish = publishpack.getPublishData();
                willPublish.will_delay = stateAgecompensatedWillDelay;

                willPublish.client_id = sender_clientid;
                willPublish.username = sender_username;

                if (settings.willsEnabled)
                    ses->setWill(std::move(willPublish));
            }
        }

        const uint32_t nrOfSubscriptions = readUint32(eofFound);
        for (uint32_t i = 0; i < nrOfSubscriptions; i++)
        {
            const std::string topic = readString(eofFound);

            logger->logf(LOG_DEBUG, "Loading subscriptions to topic '%s'.", topic.c_str());

            const uint32_t nrOfClientIds = readUint32(eofFound);

            for (uint32_t i = 0; i < nrOfClientIds; i++)
            {
                std::string sharename;
                if (readVersion >= ReadVersion::v4)
                    sharename = readString(eofFound);

                std::string clientId = readString(eofFound);
                const SubscriptionOptionsByte subscriptionOptions(readUint8(eofFound));

                uint32_t subscription_identifier = 0;
                if (readVersion >= ReadVersion::v6)
                    subscription_identifier = readUint32(eofFound);

                logger->logf(LOG_DEBUG, "Loading session '%s' subscription to '%s' QoS %d.", clientId.c_str(), topic.c_str(), subscriptionOptions.getQos());

                SubscriptionForSerializing sub(std::move(clientId), subscriptionOptions, subscription_identifier, sharename);
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
    logger->log(LOG_DEBUG) << "Saving current time stamp " << now_epoch << ".";
    writeInt64(now_epoch);

    std::vector<std::shared_ptr<Session>> sessionsToSave;
    // Sessions created with clean session need to be destroyed when disconnecting, so no point in saving them.
    std::copy_if(sessions.begin(), sessions.end(), std::back_inserter(sessionsToSave), [](const std::shared_ptr<Session> &ses) {
        return ses && !ses->destroyOnDisconnect;
    });

    writeUint32(sessionsToSave.size());

    CirBuf cirbuf(1024);

    for (std::shared_ptr<Session> &ses : sessionsToSave)
    {
        {
            MutexLocked<Session::QoSData> qos_locked = ses->qos.lock();

            logger->logf(LOG_DEBUG, "Saving session '%s'.", ses->getClientId().c_str());

            writeRowHeader();

            writeCheck(reserved, 1, RESERVED_SPACE_SESSIONS_DB_V2, f);

            writeString(ses->username);
            writeString(ses->client_id);
            writeOptionalString(ses->fmq_client_group_id);

            const size_t qosPacketsExpected = qos_locked->qosPacketQueue.size();
            size_t qosPacketsCounted = 0;
            writeUint32(qosPacketsExpected);

            std::shared_ptr<QueuedPublish> qp = qos_locked->qosPacketQueue.getTail();
            while (qp)
            {
                qosPacketsCounted++;

                Publish &pub = qp->getPublish();

                assert(!pub.skipTopic);
                assert(pub.topicAlias == 0);

                logger->logf(LOG_DEBUG, "Saving QoS %d message for topic '%s'.", pub.qos, pub.topic.c_str());

                MqttPacket pack(ProtocolVersion::Mqtt5, pub);
                pack.setPacketId(qp->getPacketId());
                const uint32_t packSize = pack.getSizeIncludingNonPresentHeader();
                cirbuf.reset();
                cirbuf.ensureFreeSpace(packSize + 32);
                pack.readIntoBuf(cirbuf);

                const uint32_t pubAge = pub.expireInfo ? ageFromTimePoint(pub.expireInfo.value().createdAt) : 0;

                writeUint16(pack.getFixedHeaderLength());
                writeUint16(qp->getPacketId());
                writeUint32(pubAge);
                writeUint32(packSize);
                writeString(pub.client_id);
                writeString(pub.username);
                writeOptionalString(qp->getTopicOverride());

                writeCheck(cirbuf.tailPtr(), 1, cirbuf.usedBytes(), f);

                qp = qp->next;
            }

            assert(qosPacketsExpected == qosPacketsCounted);

            writeUint32(qos_locked->incomingQoS2MessageIds.size());
            for (uint16_t id : qos_locked->incomingQoS2MessageIds)
            {
                logger->logf(LOG_DEBUG, "Writing incomming QoS2 message id %d.", id);
                writeUint16(id);
            }

            writeUint32(qos_locked->outgoingQoS2MessageIds.size());
            for (uint16_t id : qos_locked->outgoingQoS2MessageIds)
            {
                logger->logf(LOG_DEBUG, "Writing outgoing QoS2 message id %d.", id);
                writeUint16(id);
            }

            logger->logf(LOG_DEBUG, "Writing next packetid %d.", qos_locked->nextPacketId);
            writeUint16(qos_locked->nextPacketId);

            writeUint32(ses->getCurrentSessionExpiryInterval());

            std::shared_ptr<WillPublish> will = ses->getWill();
            const bool hasWillThatShouldSurviveRestart = will.operator bool() && will->will_delay > 0;
            writeUint16(static_cast<uint16_t>(hasWillThatShouldSurviveRestart));

            if (hasWillThatShouldSurviveRestart)
            {
                MqttPacket willpacket(ProtocolVersion::Mqtt5, *will);

                // Dummy, to please the parser on reading.
                if (will->qos > 0)
                    willpacket.setPacketId(666);

                const uint32_t packSize = willpacket.getSizeIncludingNonPresentHeader();
                cirbuf.reset();
                cirbuf.ensureFreeSpace(packSize + 32);
                willpacket.readIntoBuf(cirbuf);

                writeUint16(willpacket.getFixedHeaderLength());
                writeUint32(will->will_delay);
                writeUint32(will->getQueuedAtAge());
                writeUint32(packSize);
                writeString(will->client_id);
                writeString(will->username);
                writeCheck(cirbuf.tailPtr(), 1, cirbuf.usedBytes(), f);
            }
        }

        // Keep flushing outside of session lock, to reduce the amount of flushing while holding that lock.
        fflush(f);
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
            if (!subscription.shareName.empty())
            {
                logger->logf(LOG_DEBUG, "Saving session '%s' subscription with sharename '%s' to '%s' QoS %d.", subscription.clientId.c_str(),
                             subscription.shareName.c_str(), topic.c_str(), subscription.qos);
            }
            else
            {
                logger->logf(LOG_DEBUG, "Saving session '%s' subscription to '%s' QoS %d.", subscription.clientId.c_str(), topic.c_str(), subscription.qos);
            }

            writeString(subscription.shareName);
            writeString(subscription.clientId);
            writeUint8(subscription.getSubscriptionOptions().b);
            writeUint32(subscription.subscriptionidentifier); // Added in file version 6.
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
    if (readVersion >= ReadVersion::v3)
        return readDataV3V4V5V6V7();

    return defaultResult;
}

