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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <exception>
#include <stdexcept>
#include <stdio.h>
#include <cstring>

#include "retainedmessagesdb.h"
#include "utils.h"
#include "logger.h"

RetainedMessagesDB::RetainedMessagesDB(const std::string &filePath) : PersistenceFile(filePath)
{

}

void RetainedMessagesDB::openWrite()
{
    PersistenceFile::openWrite(MAGIC_STRING_V1);
}

void RetainedMessagesDB::openRead()
{
    PersistenceFile::openRead();

    if (detectedVersionString == MAGIC_STRING_V1)
        readVersion = ReadVersion::v1;
    else
        throw std::runtime_error("Unknown file version.");
}

/**
 * @brief RetainedMessagesDB::writeRowHeader writes two 32 bit integers: topic size and payload size.
 * @param rm
 *
 * So, the header per message is 8 bytes long.
 *
 * It writes no information about the length of the QoS value, because that is always one.
 */
void RetainedMessagesDB::writeRowHeader(const RetainedMessage &rm)
{
    writeUint32(rm.topic.size());
    writeUint32(rm.payload.size());
}

RetainedMessagesDB::RowHeader RetainedMessagesDB::readRowHeaderV1(bool &eofFound)
{
    RetainedMessagesDB::RowHeader result;

    result.topicLen = readUint32(eofFound);
    result.payloadLen = readUint32(eofFound);

    return  result;
}

/**
 * @brief RetainedMessagesDB::saveData doesn't explicitely name a file version (v1, etc), because we always write the current definition.
 * @param messages
 */
void RetainedMessagesDB::saveData(const std::vector<RetainedMessage> &messages)
{
    if (!f)
        return;

    char reserved[RESERVED_SPACE_RETAINED_DB_V1];
    std::memset(reserved, 0, RESERVED_SPACE_RETAINED_DB_V1);

    char qos = 0;
    for (const RetainedMessage &rm : messages)
    {
        logger->logf(LOG_DEBUG, "Saving retained message for topic '%s' QoS %d.", rm.topic.c_str(), rm.qos);

        writeRowHeader(rm);
        qos = rm.qos;
        writeCheck(&qos, 1, 1, f);
        writeCheck(reserved, 1, RESERVED_SPACE_RETAINED_DB_V1, f);
        writeCheck(rm.topic.c_str(), 1, rm.topic.length(), f);
        writeCheck(rm.payload.c_str(), 1, rm.payload.length(), f);
    }

    fflush(f);
}

std::list<RetainedMessage> RetainedMessagesDB::readData()
{
    std::list<RetainedMessage> defaultResult;

    if (!f)
        return defaultResult;

    if (readVersion == ReadVersion::v1)
        return readDataV1();

    return defaultResult;
}

std::list<RetainedMessage> RetainedMessagesDB::readDataV1()
{
    std::list<RetainedMessage> messages;

    while (!feof(f))
    {
        bool eofFound = false;
        RetainedMessagesDB::RowHeader header = readRowHeaderV1(eofFound);

        if (eofFound)
            continue;

        makeSureBufSize(header.payloadLen);

        readCheck(buf.data(), 1, 1, f);
        char qos = buf[0];
        fseek(f, RESERVED_SPACE_RETAINED_DB_V1, SEEK_CUR);

        readCheck(buf.data(), 1, header.topicLen, f);
        std::string topic(buf.data(), header.topicLen);

        readCheck(buf.data(), 1, header.payloadLen, f);
        std::string payload(buf.data(), header.payloadLen);

        RetainedMessage msg(topic, payload, qos);
        logger->logf(LOG_DEBUG, "Loading retained message for topic '%s' QoS %d.", msg.topic.c_str(), msg.qos);
        messages.push_back(std::move(msg));
    }

    return messages;
}
