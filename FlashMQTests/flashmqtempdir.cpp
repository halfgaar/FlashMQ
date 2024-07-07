#include "flashmqtempdir.h"

#include <sys/types.h>
#include <unistd.h>
#include <filesystem>

#include "utils.h"

FlashMQTempDir::FlashMQTempDir()
{
    const std::string templateName(std::filesystem::temp_directory_path() / "flashmq_test_XXXXXX");
    std::vector<char> nameBuf(templateName.size() + 1, 0);
    std::copy(templateName.begin(), templateName.end(), nameBuf.begin());
    this->path = std::string(mkdtemp(nameBuf.data()));
}

FlashMQTempDir::~FlashMQTempDir()
{
    if (this->path.empty() || !strContains(this->path, "flashmq_test_"))
        return;

    // Not pretty, but whatever works...
    int pid = fork();
    if (pid == 0)
    {
        execlp("rm", "rm", "-rf", "--", this->path.c_str(), (char*)NULL);
    }
}

const std::filesystem::path &FlashMQTempDir::getPath() const
{
    return this->path;
}
