#ifndef FLASHMQTEMPDIR_H
#define FLASHMQTEMPDIR_H

#include <stdlib.h>
#include <string>
#include <vector>

class FlashMQTempDir
{
    std::string path;

public:
    FlashMQTempDir();
    ~FlashMQTempDir();
    const std::string &getPath() const;
};

#endif // FLASHMQTEMPDIR_H
