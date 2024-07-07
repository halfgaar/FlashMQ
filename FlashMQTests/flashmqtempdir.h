#ifndef FLASHMQTEMPDIR_H
#define FLASHMQTEMPDIR_H

#include <filesystem>
#include <stdlib.h>
#include <string>
#include <vector>

class FlashMQTempDir
{
    std::filesystem::path path;

public:
    FlashMQTempDir();
    ~FlashMQTempDir();
    const std::filesystem::path &getPath() const;
};

#endif // FLASHMQTEMPDIR_H
