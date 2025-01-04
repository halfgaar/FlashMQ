/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "persistencefile.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <exception>
#include <stdexcept>
#include <stdio.h>
#include <cstring>
#include <libgen.h>
#include <fstream>

#include "utils.h"
#include "logger.h"

PersistenceFile::PersistenceFile(const std::string &filePath) :
    digestContext(EVP_MD_CTX_new()),
    buf(1024*1024)
{
    if (!filePath.empty() && filePath[filePath.size() - 1] == '/')
        throw std::runtime_error("Target file can't contain trailing slash.");

    this->filePath = filePath;
    this->filePathTemp = formatString("%s.newfile.%s", filePath.c_str(), getSecureRandomString(8).c_str());
    this->filePathCorrupt = formatString("%s.corrupt.%s", filePath.c_str(), getSecureRandomString(8).c_str());

    std::vector<char> d1(filePath.length() + 1, 0);
    std::copy(filePath.begin(), filePath.end(), d1.begin());
    this->dirPath = std::string(dirname(d1.data()));
}

PersistenceFile::~PersistenceFile()
{
    try
    {
        closeFile();
    }
    catch(std::exception &ex)
    {
        Logger::getInstance()->logf(LOG_WARNING, ex.what());
    }

    if (f != nullptr)
    {
        fclose(f); // fclose was already attempted and error handled if it was possible. In case an early fault happend, we need to still make sure.
        f = nullptr;
    }

    EVP_MD_CTX_free(digestContext);
    digestContext = nullptr;
}

/**
 * @brief RetainedMessagesDB::hashFile hashes the data after the headers and writes the hash in the header. Uses SHA512.
 *
 */
void PersistenceFile::writeCheck(const void *ptr, size_t size, size_t n, FILE *s)
{
    if (fwrite(ptr, size, n, s) != n)
    {
        throw std::runtime_error(formatString("Error writing: %s", strerror(errno)));
    }
}

ssize_t PersistenceFile::readCheck(void *ptr, size_t size, size_t n, FILE *stream)
{
    size_t nread = fread(ptr, size, n, stream);

    if (nread != n)
    {
        if (feof(f))
            return -1;

        throw std::runtime_error(formatString("Error reading: %s", strerror(errno)));
    }

    return nread;
}

void PersistenceFile::hashFile()
{
    logger->logf(LOG_DEBUG, "Calculating and saving hash of '%s'.", filePath.c_str());

    fseek(f, TOTAL_HEADER_SIZE, SEEK_SET);

    unsigned int output_len = 0;
    unsigned char md_value[EVP_MAX_MD_SIZE];
    std::memset(md_value, 0, EVP_MAX_MD_SIZE);

    EVP_MD_CTX_reset(digestContext);
    EVP_DigestInit_ex(digestContext, sha512, NULL);

    while (!feof(f))
    {
        size_t n = fread(buf.data(), 1, buf.size(), f);
        EVP_DigestUpdate(digestContext, buf.data(), n);
    }

    EVP_DigestFinal_ex(digestContext, md_value, &output_len);

    if (output_len != HASH_SIZE)
        throw std::runtime_error("Impossible: calculated hash size wrong length");

    fseek(f, MAGIC_STRING_LENGH, SEEK_SET);

    writeCheck(md_value, output_len, 1, f);
}

void PersistenceFile::verifyHash()
{
    fseek(f, 0, SEEK_END);
    const size_t size = ftell(f);

    if (size < TOTAL_HEADER_SIZE)
        throw std::runtime_error(formatString("File '%s' is too small for it even to contain a header.", filePath.c_str()));

    unsigned char md_from_disk[HASH_SIZE];
    std::memset(md_from_disk, 0, HASH_SIZE);

    fseek(f, MAGIC_STRING_LENGH, SEEK_SET);
    readCheck(md_from_disk, 1, HASH_SIZE, f);

    unsigned int output_len = 0;
    unsigned char md_value[EVP_MAX_MD_SIZE];
    std::memset(md_value, 0, EVP_MAX_MD_SIZE);

    EVP_MD_CTX_reset(digestContext);
    EVP_DigestInit_ex(digestContext, sha512, NULL);

    while (!feof(f))
    {
        size_t n = fread(buf.data(), 1, buf.size(), f);
        EVP_DigestUpdate(digestContext, buf.data(), n);
    }

    EVP_DigestFinal_ex(digestContext, md_value, &output_len);

    if (output_len != HASH_SIZE)
        throw std::runtime_error("Impossible: calculated hash size wrong length");

    if (std::memcmp(md_from_disk, md_value, output_len) != 0)
    {
        fclose(f);
        f = nullptr;

        if (rename(filePath.c_str(), filePathCorrupt.c_str()) == 0)
        {
            throw std::runtime_error(formatString("File '%s' is corrupt: hash mismatch. Moved aside to '%s'.", filePath.c_str(), filePathCorrupt.c_str()));
        }
        else
        {
            throw std::runtime_error(formatString("File '%s' is corrupt: hash mismatch. Tried to move aside, but that failed: '%s'.",
                                                  filePath.c_str(), strerror(errno)));
        }
    }

    logger->logf(LOG_DEBUG, "Hash of '%s' correct", filePath.c_str());
}

/**
 * @brief PersistenceFile::makeSureBufSize grows the buffer if n is bigger.
 * @param n in bytes.
 *
 * Remember that when you're dealing with fields that are sized in MQTT by 16 bit ints, like topic paths, the buffer will always be big enough, because it's 1 MB.
 */
void PersistenceFile::makeSureBufSize(size_t n)
{
    if (n > buf.size())
        buf.resize(n);
}

void PersistenceFile::writeInt64(const int64_t val)
{
    unsigned char buf[8];

    // Write big-endian
    int shift = 56;
    int i = 0;
    while (shift >= 0)
    {
        unsigned char wantedByte = val >> shift;
        buf[i++] = wantedByte;
        shift -= 8;
    }
    writeCheck(buf, 1, 8, f);
}

void PersistenceFile::writeUint32(const uint32_t val)
{
    unsigned char buf[4];

    // Write big-endian
    int shift = 24;
    int i = 0;
    while (shift >= 0)
    {
        unsigned char wantedByte = val >> shift;
        buf[i++] = wantedByte;
        shift -= 8;
    }
    writeCheck(buf, 1, 4, f);
}

void PersistenceFile::writeUint16(const uint16_t val)
{
    unsigned char buf[2];

    // Write big-endian
    int shift = 8;
    int i = 0;
    while (shift >= 0)
    {
        unsigned char wantedByte = val >> shift;
        buf[i++] = wantedByte;
        shift -= 8;
    }
    writeCheck(buf, 1, 2, f);
}

void PersistenceFile::writeUint8(const uint8_t val)
{
    writeCheck(&val, 1, 1, f);
}

void PersistenceFile::writeString(const std::string &s)
{
    writeUint32(s.size());
    writeCheck(s.c_str(), 1, s.size(), f);
}

void PersistenceFile::writeOptionalString(const std::optional<std::string> &s)
{
    if (!s)
    {
        writeUint8(0);
        return;
    }

    writeUint8(1);
    writeString(s.value());
}

int64_t PersistenceFile::readInt64(bool &eofFound)
{
    if (readCheck(buf.data(), 1, 8, f) < 0)
        eofFound = true;

    unsigned char *buf_ = reinterpret_cast<unsigned char *>(buf.data());
    const uint64_t val1 = ((buf_[0]) << 24) | ((buf_[1]) << 16) | ((buf_[2]) << 8) | (buf_[3]);
    const uint64_t val2 = ((buf_[4]) << 24) | ((buf_[5]) << 16) | ((buf_[6]) << 8) | (buf_[7]);
    const int64_t val = (val1 << 32) | val2;
    return val;
}

uint32_t PersistenceFile::readUint32(bool &eofFound)
{
    if (readCheck(buf.data(), 1, 4, f) < 0)
        eofFound = true;

    uint32_t val;
    unsigned char *buf_ = reinterpret_cast<unsigned char *>(buf.data());
    val = ((buf_[0]) << 24) | ((buf_[1]) << 16) | ((buf_[2]) << 8) | (buf_[3]);
    return val;
}

uint16_t PersistenceFile::readUint16(bool &eofFound)
{
    if (readCheck(buf.data(), 1, 2, f) < 0)
        eofFound = true;

    uint16_t val;
    unsigned char *buf_ = reinterpret_cast<unsigned char *>(buf.data());
    val = ((buf_[0]) << 8) | (buf_[1]);
    return val;
}

uint8_t PersistenceFile::readUint8(bool &eofFound)
{
    uint8_t val;

    if (readCheck(&val, 1, 1, f) < 0)
        eofFound = true;

    return val;
}

std::string PersistenceFile::readString(bool &eofFound)
{
    const uint32_t size = readUint32(eofFound);

    if (size > 0xFFFF)
        throw std::runtime_error("In MQTT world, strings are never longer than 65535 bytes.");

    makeSureBufSize(size);
    readCheck(buf.data(), 1, size, f);
    std::string result(buf.data(), size);
    return result;
}

std::optional<std::string> PersistenceFile::readOptionalString(bool &eofFound)
{
    uint8_t x = readUint8(eofFound);

    if (!x)
        return {};

    std::optional<std::string> result = readString(eofFound);
    return result;
}

/**
 * @brief RetainedMessagesDB::openWrite doesn't explicitely name a file version (v1, etc), because we always write the current definition.
 */
void PersistenceFile::openWrite(const std::string &versionString)
{
    if (openMode != FileMode::unknown)
        throw std::runtime_error("File is already open.");

    f = fopen(filePathTemp.c_str(), "w+b");

    if (f == nullptr)
    {
        throw std::runtime_error(formatString("Can't open '%s': %s", filePathTemp.c_str(), strerror(errno)));
    }

    chmod(filePathTemp.c_str(), S_IRUSR | S_IWUSR);

    openMode = FileMode::write;

    writeCheck(buf.data(), 1, MAGIC_STRING_LENGH, f);
    rewind(f);
    writeCheck(versionString.c_str(), 1, versionString.length(), f);
    fseek(f, MAGIC_STRING_LENGH, SEEK_SET);
    writeCheck(buf.data(), 1, HASH_SIZE, f);
}

void PersistenceFile::openRead(const std::string &expected_version_string)
{
    if (openMode != FileMode::unknown)
        throw std::runtime_error("File is already open.");

    f = fopen(filePath.c_str(), "rb");

    if (f == nullptr)
        throw PersistenceFileCantBeOpened(formatString("Can't open '%s': %s.", filePath.c_str(), strerror(errno)).c_str());

    openMode = FileMode::read;

    verifyHash();
    rewind(f);

    readCheck(buf.data(), 1, MAGIC_STRING_LENGH, f);
    detectedVersionString = std::string(buf.data(), strlen(buf.data()));

    // In case people want to downgrade, the old file is still there.
    if (detectedVersionString != expected_version_string)
    {
        const std::string copy_file_path = filePath + "." + detectedVersionString;

        try
        {
            const size_t file_size = getFileSize(filePath);
            const size_t free_space = getFreeSpace(filePath);

            if (free_space > file_size * 3)
            {
                logger->log(LOG_NOTICE) << "File version change detected. Copying '" << filePath << "' to '"
                                        << copy_file_path << "' to support downgrading FlashMQ.";
                std::ifstream  src(filePath, std::ios::binary);
                std::ofstream  dst(copy_file_path, std::ios::binary);
                src.exceptions(std::ifstream::failbit | std::ifstream::badbit);
                dst.exceptions(std::ifstream::failbit | std::ifstream::badbit);
                dst << src.rdbuf();
            }
            else
            {
                logger->log(LOG_NOTICE) << "File version change detected, but not copying '" << filePath << "' to '"
                                        << copy_file_path << "' because disk space is running low.";
            }
        }
        catch (std::exception &ex)
        {
            logger->log(LOG_ERROR) << "Backing up to '" << copy_file_path << "' failed.";
        }
    }

    fseek(f, TOTAL_HEADER_SIZE, SEEK_SET);
}

void PersistenceFile::dontSaveTmpFile()
{
    this->discard = true;
}

void PersistenceFile::closeFile()
{
    if (!f)
        return;

    if (openMode == FileMode::write)
    {
        if (!discard)
            hashFile();

        if (fflush(f) != 0)
        {
            std::string msg(strerror(errno));
            throw std::runtime_error(formatString("Flush of '%s' failed: %s.", this->filePathTemp.c_str(), msg.c_str()));
        }

        fsync(f->_fileno);
    }

    if (f != nullptr)
    {
        FILE *f2 = f;
        f = nullptr;

        if (fclose(f2) < 0)
        {
            std::string msg(strerror(errno));
            throw std::runtime_error(formatString("Close of '%s' failed: %s.", this->filePathTemp.c_str(), msg.c_str()));
        }
    }

    if (openMode == FileMode::write && !filePathTemp.empty() && ! filePath.empty())
    {
        if (discard)
        {
            unlink(filePathTemp.c_str());
        }
        else
        {
            if (rename(filePathTemp.c_str(), filePath.c_str()) < 0)
                throw std::runtime_error(formatString("Saving '%s' failed: rename of temp file to target failed with: %s", filePath.c_str(), strerror(errno)));

            int dir_fd = open(this->dirPath.c_str(), O_RDONLY);
            fsync(dir_fd);
            close(dir_fd);
        }
    }
}

const std::string &PersistenceFile::getFilePath() const
{
    return this->filePath;
}
