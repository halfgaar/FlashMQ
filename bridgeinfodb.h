#ifndef BRIDGEINFODB_H
#define BRIDGEINFODB_H

#include <list>

#include "persistencefile.h"
#include "bridgeconfig.h"


#define MAGIC_STRING_BRIDGEINFO_FILE_V1 "BridgeInfoDbV1"

struct BridgeInfoForSerializing
{
    std::string prefix;
    std::string clientId;

    BridgeInfoForSerializing() = default;
    BridgeInfoForSerializing(const std::shared_ptr<BridgeConfig> bridge);

    static std::list<BridgeInfoForSerializing> getBridgeInfosForSerializing(const std::unordered_map<std::string, std::shared_ptr<BridgeConfig>> &input);
};

class BridgeInfoDb : private PersistenceFile
{
    enum class ReadVersion
    {
        unknown,
        v1,
    };

    ReadVersion readVersion = ReadVersion::unknown;

public:
    BridgeInfoDb(const std::string &filePath);

    void openWrite();
    void openRead();

    void saveInfo(const std::list<BridgeInfoForSerializing> &bridgeInfos);
    std::list<BridgeInfoForSerializing> readInfo();
};

#endif // BRIDGEINFODB_H
