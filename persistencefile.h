/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef PERSISTENCEFILE_H
#define PERSISTENCEFILE_H

#include <vector>
#include <list>
#include <string>
#include <stdio.h>
#include <openssl/evp.h>
#include <stdexcept>
#include <cstring>
#include <optional>

#include "logger.h"

#define MAGIC_STRING_LENGH 32
#define HASH_SIZE 64
#define TOTAL_HEADER_SIZE (MAGIC_STRING_LENGH + HASH_SIZE)

/**
 * @brief The PersistenceFileCantBeOpened class should be thrown when a non-fatal file-not-found error happens.
 */
class PersistenceFileCantBeOpened : public std::runtime_error
{
public:
    PersistenceFileCantBeOpened(const std::string &msg) : std::runtime_error(msg) {}
};

class PersistenceFile
{
    std::string filePath;
    std::string filePathTemp;
    std::string filePathCorrupt;
    std::string dirPath;
    bool discard = false;

    EVP_MD_CTX *digestContext = nullptr;
    const EVP_MD *sha512 = EVP_sha512();

    void hashFile();
    void verifyHash();

protected:
    enum class FileMode
    {
        unknown,
        read,
        write
    };

    FILE *f = nullptr;
    std::vector<unsigned char> buf;
    FileMode openMode = FileMode::unknown;
    std::string detectedVersionString;

    Logger *logger = Logger::getInstance();

    void makeSureBufSize(size_t n);

    void writeCheck(const void *__restrict __ptr, size_t __size, size_t __n, FILE *__restrict __s);
    ssize_t readCheck(void *__restrict ptr, size_t size, size_t n, FILE *__restrict stream);

    void writeInt64(const int64_t val);
    void writeUint32(const uint32_t val);
    void writeUint16(const uint16_t val);
    void writeUint8(const uint8_t val);
    void writeString(const std::string &s);
    void writeOptionalString(const std::optional<std::string> &s);
    int64_t readInt64(bool &eofFound);
    uint32_t readUint32(bool &eofFound);
    uint16_t readUint16(bool &eofFound);
    uint8_t readUint8(bool &eofFound);
    std::string readString(bool &eofFound);
    std::optional<std::string> readOptionalString(bool &eofFound);

public:
    PersistenceFile(const std::string &filePath);
    virtual ~PersistenceFile();

    void openWrite(const std::string &versionString);
    void openRead(const std::string &expected_version_string);
    void dontSaveTmpFile();
    void closeFile();

    const std::string &getFilePath() const;
};

#endif // PERSISTENCEFILE_H
