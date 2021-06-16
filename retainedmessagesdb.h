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

#ifndef RETAINEDMESSAGESDB_H
#define RETAINEDMESSAGESDB_H

#include "persistencefile.h"
#include "retainedmessage.h"

#include "logger.h"

#define MAGIC_STRING_V1 "FlashMQRetainedDBv1"
#define ROW_HEADER_SIZE 8
#define RESERVED_SPACE_RETAINED_DB_V1 31

/**
 * @brief The RetainedMessagesDB class saves and loads the retained messages.
 *
 * The DB looks like, from the top:
 *
 * MAGIC_STRING_LENGH bytes file header
 * HASH_SIZE SHA512
 * [MESSAGES]
 *
 * Each message has a row header, which is 8 bytes. See writeRowHeader().
 *
 */
class RetainedMessagesDB : public PersistenceFile
{
    enum class ReadVersion
    {
        unknown,
        v1
    };

    struct RowHeader
    {
        uint32_t topicLen = 0;
        uint32_t payloadLen = 0;
    };

    ReadVersion readVersion = ReadVersion::unknown;

    void writeRowHeader(const RetainedMessage &rm);
    RowHeader readRowHeaderV1(bool &eofFound);
    std::list<RetainedMessage> readDataV1();
public:
    RetainedMessagesDB(const std::string &filePath);

    void openWrite();
    void openRead();

    void saveData(const std::vector<RetainedMessage> &messages);
    std::list<RetainedMessage> readData();
};

#endif // RETAINEDMESSAGESDB_H
