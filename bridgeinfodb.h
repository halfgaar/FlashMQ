#ifndef BRIDGEINFODB_H
#define BRIDGEINFODB_H

#include <list>

#include "persistencefile.h"
#include "bridgeconfig.h"


#define MAGIC_STRING_BRIDGEINFO_FILE_V1 "BridgeInfoDbV1"
#define MAGIC_STRING_BRIDGEINFO_FILE_V2 "BridgeInfoDbV2"

struct BridgeInfoForSerializing
{
    std::string prefix;
    std::string clientId;
    std::string client_group_share_name;

    BridgeInfoForSerializing() = default;
    BridgeInfoForSerializing(const BridgeConfig &bridge);

    static std::list<BridgeInfoForSerializing> getBridgeInfosForSerializing(const std::unordered_map<std::string, BridgeConfig> &input);
};

class BridgeInfoDb : private PersistenceFile
{
    enum class ReadVersion
    {
        unknown,
        v1,
        v2
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
