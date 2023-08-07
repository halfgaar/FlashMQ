/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef RETAINEDMESSAGESDB_H
#define RETAINEDMESSAGESDB_H

#include "persistencefile.h"
#include "retainedmessage.h"

#define MAGIC_STRING_V1 "FlashMQRetainedDBv1"
#define MAGIC_STRING_V2 "FlashMQRetainedDBv2"
#define MAGIC_STRING_V3 "FlashMQRetainedDBv3"
#define MAGIC_STRING_V4 "FlashMQRetainedDBv4"
#define RESERVED_SPACE_RETAINED_DB_V2 64

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
class RetainedMessagesDB : private PersistenceFile
{
    enum class ReadVersion
    {
        unknown,
        v1,
        v2,
        v3,
        v4
    };

    struct RowHeader
    {
        uint32_t topicLen = 0;
        uint32_t payloadLen = 0;
    };

    ReadVersion readVersion = ReadVersion::unknown;

    std::list<RetainedMessage> readDataV3V4();
public:
    RetainedMessagesDB(const std::string &filePath);

    void openWrite();
    void openRead();
    void closeFile();

    void saveData(const std::vector<RetainedMessage> &messages);
    std::list<RetainedMessage> readData();
};

#endif // RETAINEDMESSAGESDB_H
