/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef GLOBALSTATS_H
#define GLOBALSTATS_H

#include <stdint.h>
#include "derivablecounter.h"

class GlobalStats
{
    static GlobalStats *instance;

    GlobalStats();
public:
    static GlobalStats *getInstance();

    DerivableCounter socketConnects;
};

#endif // GLOBALSTATS_H
