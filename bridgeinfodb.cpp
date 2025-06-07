#include "bridgeinfodb.h"

using std::unordered_map;

BridgeInfoForSerializing::BridgeInfoForSerializing(const BridgeConfig &bridge) :
    prefix(bridge.clientidPrefix),
    clientId(bridge.getClientid())
{

}

std::list<BridgeInfoForSerializing> BridgeInfoForSerializing::getBridgeInfosForSerializing(const unordered_map<std::string, BridgeConfig> &input)
{
    std::list<BridgeInfoForSerializing> result;

    for (auto &pair : input)
    {
        const BridgeConfig &bridge = pair.second;
        result.emplace_back(bridge);
    }

    return result;
}

BridgeInfoDb::BridgeInfoDb(const std::string &filePath) : PersistenceFile(filePath)
{

}

void BridgeInfoDb::openWrite()
{
    PersistenceFile::openWrite(MAGIC_STRING_BRIDGEINFO_FILE_V1);
}

void BridgeInfoDb::openRead()
{
    const std::string current_magic_string(MAGIC_STRING_BRIDGEINFO_FILE_V1);
    PersistenceFile::openRead(current_magic_string);

    if (detectedVersionString == current_magic_string)
        readVersion = ReadVersion::v1;
    else
        throw std::runtime_error("Unknown file version.");
}

void BridgeInfoDb::saveInfo(const std::list<BridgeInfoForSerializing> &bridgeInfos)
{
    if (!f)
        return;

    writeUint32(bridgeInfos.size());

    for (const BridgeInfoForSerializing &b : bridgeInfos)
    {
        writeString(b.prefix);
        writeString(b.clientId);
    }
}

std::list<BridgeInfoForSerializing> BridgeInfoDb::readInfo()
{
    std::list<BridgeInfoForSerializing> result;

    if (!f)
        return result;

    while (!feof(f))
    {
        bool eofFound = false;

        uint32_t number_of_bridges = readUint32(eofFound);

        if (eofFound)
            continue;

        for (uint32_t i = 0; i < number_of_bridges; i++)
        {
            BridgeInfoForSerializing r;

            r.prefix = readString(eofFound);
            r.clientId = readString(eofFound);

            result.push_back(std::move(r));
        }

    }

    return result;
}


