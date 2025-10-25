/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2025 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "persistencefunctions.h"
#include "logger.h"
#include "globals.h"
#include "globber.h"

/**
 * @brief saveState saves sessions and such to files. It's run in the main thread, but also dedicated threads. For that,
 * reason, it's a static method to reduce the risk of accidental use of data without locks.
 * @param settings A local settings, copied from a std::bind copy when running in a thread, because of thread safety.
 * @param bridgeInfos is a list of objects already prepared from the original bridge configs, to avoid concurrent access.
 */
void saveState(const Settings &settings, const std::list<BridgeInfoForSerializing> &bridgeInfos, bool in_background)
{
    Logger *logger = Logger::getInstance();

    try
    {
        if (settings.storageDir.empty())
            return;

        if (settings.persistenceDataToSave.hasNone())
            return;

        std::shared_ptr<SubscriptionStore> subscriptionStore = globals->subscriptionStore;

        if (settings.persistenceDataToSave.hasFlagSet(PersistenceDataToSave::RetainedMessages)
            && settings.retainedMessagesMode == RetainedMessagesMode::Enabled)
        {
            const std::string retainedDBPath = settings.getRetainedMessagesDBFile();
            subscriptionStore->saveRetainedMessages(retainedDBPath, in_background);
        }

        if (settings.persistenceDataToSave.hasFlagSet(PersistenceDataToSave::SessionsAndSubscriptions))
        {
            const std::string sessionsDBPath = settings.getSessionsDBFile();
            subscriptionStore->saveSessionsAndSubscriptions(sessionsDBPath);
        }

        if (settings.persistenceDataToSave.hasFlagSet(PersistenceDataToSave::BridgeInfo))
        {
            saveBridgeInfo(settings.getBridgeNamesDBFile(), bridgeInfos);
        }

        logger->logf(LOG_NOTICE, "Saving states done");
    }
    catch(std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error saving state: %s", ex.what());
    }
}

void saveBridgeInfo(const std::string &filePath, const std::list<BridgeInfoForSerializing> &bridgeInfos)
{
    Logger *logger = Logger::getInstance();
    logger->logf(LOG_NOTICE, "Saving bridge info in '%s'", filePath.c_str());
    BridgeInfoDb bridgeInfoDb(filePath);
    bridgeInfoDb.openWrite();
    bridgeInfoDb.saveInfo(bridgeInfos);
}

std::list<BridgeConfig> loadBridgeInfo(Settings &settings)
{
    Logger *logger = Logger::getInstance();
    std::list<BridgeConfig> bridges = settings.stealBridges();

    if (settings.storageDir.empty())
        return bridges;

    const std::string filePath = settings.getBridgeNamesDBFile();

    try
    {
        logger->logf(LOG_NOTICE, "Loading '%s'", filePath.c_str());

        BridgeInfoDb dbfile(filePath);
        dbfile.openRead();
        std::list<BridgeInfoForSerializing> bridgeInfos = dbfile.readInfo();

        for(const BridgeInfoForSerializing &info : bridgeInfos)
        {
            for(BridgeConfig &bridgeConfig : bridges)
            {
                if (!bridgeConfig.useSavedClientId)
                    continue;

                if (bridgeConfig.clientidPrefix == info.prefix)
                {
                    logger->log(LOG_INFO) << "Assigning stored bridge clientid '" << info.clientId << "' to bridge '" << info.prefix << "'.";
                    bridgeConfig.setClientId(info.prefix, info.clientId);
                    break;
                }
            }
        }
    }
    catch (PersistenceFileCantBeOpened &ex)
    {
        logger->logf(LOG_WARNING, "File '%s' is not there (yet)", filePath.c_str());
    }

    return bridges;
}

void correctBackupDbPermissions(const std::string &dir)
{
    Globber glob;
    const std::string backups_glob = dir + "/*.db.*";
    auto matches = glob.getGlob(backups_glob);

    for (const auto &s : matches)
    {
        chmod(s.c_str(), S_IRUSR | S_IWUSR);
    }
}
