/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <exception>
#include <stdexcept>
#include <stdio.h>
#include <cstring>
#include <inttypes.h>

#include "retainedmessagesdb.h"
#include "utils.h"
#include "logger.h"
#include "mqttpacket.h"
#include "threadglobals.h"
#include "client.h"
#include "logger.h"

RetainedMessagesDB::RetainedMessagesDB(const std::string &filePath) : PersistenceFile(filePath)
{

}

RetainedMessagesDB::~RetainedMessagesDB()
{
    closeFile();
}

void RetainedMessagesDB::openWrite()
{
    PersistenceFile::openWrite(MAGIC_STRING_V4);

    this->written_count = 0;

    const int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    logger->log(LOG_DEBUG) << "Saving current time stamp " << now_epoch << " in retained messages DB.";
    writeInt64(now_epoch);

    length_pos = ftell(f);
    writeUint32(0);

    char reserved[RESERVED_SPACE_RETAINED_DB_V2];
    std::memset(reserved, 0, RESERVED_SPACE_RETAINED_DB_V2);
    writeCheck(reserved, 1, RESERVED_SPACE_RETAINED_DB_V2, f);
}

void RetainedMessagesDB::openRead()
{
    const std::string current_magic_string(MAGIC_STRING_V4);

    PersistenceFile::openRead(current_magic_string);

    if (detectedVersionString == MAGIC_STRING_V1)
        readVersion = ReadVersion::v1;
    else if (detectedVersionString == MAGIC_STRING_V2)
        readVersion = ReadVersion::v2;
    else if (detectedVersionString == MAGIC_STRING_V3)
        readVersion = ReadVersion::v3;
    else if (detectedVersionString == current_magic_string)
        readVersion = ReadVersion::v4;
    else
        throw std::runtime_error("Unknown file version.");

    bool eofFound = false;

    if (readVersion >= ReadVersion::v4)
    {
        const int64_t fileSavedAt = readInt64(eofFound);
        if (eofFound)
            throw std::runtime_error("Error reading retained messages file age: eof reached.");

        const int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        persistence_state_age = fileSavedAt > now_epoch ? 0 : now_epoch - fileSavedAt;
    }

    to_read_count = readUint32(eofFound);

    if (eofFound)
        throw std::runtime_error("Error reading retained messages file message count: eof reached.");

    fseek(f, RESERVED_SPACE_RETAINED_DB_V2, SEEK_CUR);
}

void RetainedMessagesDB::closeFile()
{
    if (!f)
        return;

    if (openMode == FileMode::write && length_pos > 0 && written_count > 0)
    {
        fseek(f, length_pos, SEEK_SET);
        writeUint32(written_count);
    }

    PersistenceFile::closeFile();
}

/**
 * @brief RetainedMessagesDB::saveData doesn't explicitely name a file version (v1, etc), because we always write the current definition.
 * @param messages
 */
void RetainedMessagesDB::saveData(const std::vector<RetainedMessage> &messages)
{
    if (!f)
        return;

    CirBuf cirbuf(1024);

    for (const RetainedMessage &rm : messages)
    {
        logger->logf(LOG_DEBUG, "Saving retained message for topic '%s' QoS %d, age %d seconds.", rm.publish.topic.c_str(), rm.publish.qos, rm.publish.getAge());

        this->written_count++;

        Publish pcopy(rm.publish);
        MqttPacket pack(ProtocolVersion::Mqtt5, pcopy);

        // Dummy, to please the parser on reading.
        if (pcopy.qos > 0)
            pack.setPacketId(666);

        const uint32_t packSize = pack.getSizeIncludingNonPresentHeader();
        const uint32_t pubAge = pcopy.expireInfo ? ageFromTimePoint(pcopy.expireInfo.value().createdAt) : 0;

        cirbuf.reset();
        cirbuf.ensureFreeSpace(packSize + 32);
        pack.readIntoBuf(cirbuf);

        writeUint16(pack.getFixedHeaderLength());
        writeUint32(pubAge);
        writeUint32(packSize);
        writeString(pcopy.client_id);
        writeString(pcopy.username);
        writeCheck(cirbuf.tailPtr(), 1, cirbuf.usedBytes(), f);
    }

    fflush(f);
}

std::list<RetainedMessage> RetainedMessagesDB::readData(size_t max)
{
    std::list<RetainedMessage> defaultResult;

    if (!f)
        return defaultResult;

    if (readVersion == ReadVersion::v1)
        logger->logf(LOG_WARNING, "File '%s' is version 1, an internal development version that was never finalized. Not reading.", getFilePath().c_str());
    if (readVersion == ReadVersion::v2)
        logger->logf(LOG_WARNING, "File '%s' is version 2, an internal development version that was never finalized. Not reading.", getFilePath().c_str());
    if (readVersion == ReadVersion::v3 || readVersion == ReadVersion::v4)
        return readDataV3V4(max);

    return defaultResult;
}

std::list<RetainedMessage> RetainedMessagesDB::readDataV3V4(size_t max)
{
    std::list<RetainedMessage> messages;

    CirBuf cirbuf(1024);

    const Settings &settings = *ThreadGlobals::getSettings();
    std::shared_ptr<ThreadData> dummyThreadData;
    std::shared_ptr<Client> dummyClient(new Client(0, dummyThreadData, nullptr, false, false, nullptr, settings, false));
    dummyClient->setClientProperties(ProtocolVersion::Mqtt5, "Dummyforloadingretained", "nobody", true, 60);

    bool eofFound = false;

    const uint32_t numberOfMessages = std::min<uint32_t>(to_read_count, max);

    for(uint32_t i = 0; i < numberOfMessages; i++)
    {
        assert(to_read_count > 0);
        to_read_count--;

        const uint16_t fixed_header_length = readUint16(eofFound);
        uint32_t originalPubAge = 0;
        if (readVersion >= ReadVersion::v4)
        {
            originalPubAge = readUint32(eofFound);
        }
        const uint32_t newPubAge = persistence_state_age + originalPubAge;
        const uint32_t packlen = readUint32(eofFound);

        const std::string client_id = readString(eofFound);
        const std::string username = readString(eofFound);

        if (eofFound)
            throw std::runtime_error("Error reading retained messages: unexpected end of file");

        cirbuf.reset();
        cirbuf.ensureFreeSpace(packlen + 32);

        readCheck(cirbuf.headPtr(), 1, packlen, f);
        cirbuf.advanceHead(packlen);
        MqttPacket pack(cirbuf, packlen, fixed_header_length, dummyClient);

        pack.parsePublishData(dummyClient);
        Publish pub(pack.getPublishData());

        pub.client_id = client_id;
        pub.username = username;

        if (pub.expireInfo)
            pub.expireInfo.value().createdAt = timepointFromAge(newPubAge);

        RetainedMessage msg(pub);
        logger->logf(LOG_DEBUG, "Loading retained message for topic '%s' QoS %d, age %d seconds.", msg.publish.topic.c_str(), msg.publish.qos, msg.publish.getAge());
        messages.push_back(std::move(msg));
    }

    return messages;
}
