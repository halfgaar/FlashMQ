/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2025 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef PERSISTENCEFUNCTIONS_H
#define PERSISTENCEFUNCTIONS_H

#include "bridgeinfodb.h"

void saveState(const Settings &settings, const std::list<BridgeInfoForSerializing> &bridgeInfos, bool in_background);
void saveBridgeInfo(const std::string &filePath, const std::list<BridgeInfoForSerializing> &bridgeInfos);
std::list<BridgeConfig> loadBridgeInfo(Settings &settings);

#endif // PERSISTENCEFUNCTIONS_H
