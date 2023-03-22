#include "conffiletemp.h"

#include <vector>
#include <unistd.h>
#include <stdexcept>

ConfFileTemp::ConfFileTemp()
{
    const std::string templateName("/tmp/flashmqconf_XXXXXX");
    std::vector<char> nameBuf(templateName.size() + 1, 0);
    std::copy(templateName.begin(), templateName.end(), nameBuf.begin());
    this->fd = mkstemp(nameBuf.data());

    if (this->fd < 0)
    {
        throw std::runtime_error("mkstemp error.");
    }

    this->filePath = nameBuf.data();
}

ConfFileTemp::~ConfFileTemp()
{
    closeFile();

    if (!this->filePath.empty())
        unlink(this->filePath.c_str());
}

const std::string &ConfFileTemp::getFilePath() const
{
    if (fd > 0)
        throw std::runtime_error("You first need to close the file before using it.");

    return this->filePath;
}

void ConfFileTemp::writeLine(const std::string &line)
{
    if (write(this->fd, line.c_str(), line.size()) < 0)
        throw std::runtime_error("Config file write failed");
    if (write(this->fd, "\n", 1) < 0)
        throw std::runtime_error("Config file write failed");
}

void ConfFileTemp::closeFile()
{
    if (this->fd < 0)
        return;

    close(this->fd);
    this->fd = -1;
}
