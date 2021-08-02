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

#include "persistencefile.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <exception>
#include <stdexcept>
#include <stdio.h>
#include <cstring>

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
}

PersistenceFile::~PersistenceFile()
{
    closeFile();

    if (digestContext)
    {
        EVP_MD_CTX_free(digestContext);
    }
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
    fflush(f);
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
    fread(md_from_disk, 1, HASH_SIZE, f);

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

void PersistenceFile::openRead()
{
    if (openMode != FileMode::unknown)
        throw std::runtime_error("File is already open.");

    f = fopen(filePath.c_str(), "rb");

    if (f == nullptr)
        throw PersistenceFileCantBeOpened(formatString("Can't open '%s': %s.", filePath.c_str(), strerror(errno)).c_str());

    openMode = FileMode::read;

    verifyHash();
    rewind(f);

    fread(buf.data(), 1, MAGIC_STRING_LENGH, f);
    detectedVersionString = std::string(buf.data(), strlen(buf.data()));

    fseek(f, TOTAL_HEADER_SIZE, SEEK_SET);
}

void PersistenceFile::closeFile()
{
    if (!f)
        return;

    if (openMode == FileMode::write)
        hashFile();

    if (f != nullptr)
    {
        fclose(f);
        f = nullptr;
    }

    if (openMode == FileMode::write && !filePathTemp.empty() && ! filePath.empty())
    {
        if (rename(filePathTemp.c_str(), filePath.c_str()) < 0)
            throw std::runtime_error(formatString("Saving '%s' failed: rename of temp file to target failed with: %s", filePath.c_str(), strerror(errno)));
    }
}
